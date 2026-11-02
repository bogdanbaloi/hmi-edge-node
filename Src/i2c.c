/**
 * @file i2c.c
 * @brief I2C1 at 100 kHz on PB8 and PB9. See i2c.h for what is proved and
 *        what is a hypothesis awaiting the board.
 */

#include "i2c.h"

#include "core.h"
#include "registers.h"

#include <stddef.h>

/* ---- Pins ---------------------------------------------------------------
 * PB8 is SCL, PB9 is SDA, both on alternate function 4. The order matters
 * for the same reason it mattered on the UART pins: a pin switched to
 * alternate mode before the function is selected drives whatever the
 * previous function was for a few cycles.
 */
#define I2C1_SCL_PIN           8U
#define I2C1_SDA_PIN           9U
#define I2C1_PIN_AF4           4U
#define GPIO_OPEN_DRAIN        1UL

/// AFRH covers pins 8 to 15, so the field index is the pin minus eight.
#define GPIO_AFRH_FIRST_PIN    8U

/* ---- Bit timing ---------------------------------------------------------
 * I2C1 takes its clock from PCLK1, which is the reset selection in
 * RCC_CCIPR and is left alone here. PCLK1 is 4 MHz on this board, the same
 * 4 MHz the UART divides for 115200 baud, so one I2C clock tick is 250 ns.
 *
 * With PRESC at 0 the tick is the timing unit, and standard mode asks for a
 * low period of at least 4.7 us and a high period of at least 4.0 us:
 *
 *   SCLL  19 -> (19 + 1) * 250 ns = 5.00 us low
 *   SCLH  15 -> (15 + 1) * 250 ns = 4.00 us high
 *
 * which puts one bit at about 9 us before rise and fall times, so a little
 * under 100 kHz. Slower than nominal is legal, faster is not.
 *
 *   SDADEL 1 -> 250 ns of data hold after the falling clock edge
 *   SCLDEL 3 -> 1.00 us of data setup before the rising edge, against the
 *               250 ns standard mode asks for
 *
 * **This value is computed, not copied from a table.** RM0351 could not be
 * downloaded on 2026-11-02. If the bus misbehaves, this constant is the
 * first thing to doubt, and a scope on PB8 settles it in a minute.
 */
#define I2C1_TIMING_PRESC      0UL
#define I2C1_TIMING_SCLDEL     3UL
#define I2C1_TIMING_SDADEL     1UL
#define I2C1_TIMING_SCLH       15UL
#define I2C1_TIMING_SCLL       19UL

#define I2C1_TIMING_PRESC_SHIFT   28U
#define I2C1_TIMING_SCLDEL_SHIFT  20U
#define I2C1_TIMING_SDADEL_SHIFT  16U
#define I2C1_TIMING_SCLH_SHIFT    8U

/**
 * How long any single flag may take before the transfer is abandoned.
 *
 * A nine byte transfer at 100 kHz takes about one millisecond, so five is
 * generous for the slowest thing that can legitimately happen. The clock
 * behind it is SysTick, which keeps running even while the bus is stuck,
 * which is the property that makes the timeout work at all.
 */
#define I2C1_TIMEOUT_MS        5U

/// Every error flag the peripheral can raise during a transfer.
#define I2C1_ISR_ANY_ERROR     (I2C1_ISR_BERR | I2C1_ISR_ARLO)
#define I2C1_ICR_ANY_ERROR     (I2C1_ICR_BERRCF | I2C1_ICR_ARLOCF)

/// Put one pin into alternate function 4, open drain, with a pull-up.
static void pin_to_i2c(uint32_t pin) {
    /* AFRH holds pins 8 to 15, so the field index is the pin minus eight,
     * while MODER and PUPDR are indexed by the pin itself. */
    const uint32_t af_pin = pin - GPIO_AFRH_FIRST_PIN;

    /* Alternate function first, mode last, so the pin never drives the old
     * function while already switched over. Same order as the UART pins in
     * board.c, for the same reason. */
    GPIOB_AFRH = (GPIOB_AFRH & ~GPIO_AF_MASK(af_pin))
                 | GPIO_AF(af_pin, I2C1_PIN_AF4);
    GPIOB_OTYPER |= (GPIO_OPEN_DRAIN << pin);
    GPIOB_PUPDR = (GPIOB_PUPDR & ~GPIO_PULL_MASK(pin))
                  | GPIO_PULL(pin, GPIO_PULL_UP);
    GPIOB_MODER = (GPIOB_MODER & ~GPIO_MODE_MASK(pin))
                  | GPIO_MODE(pin, GPIO_MODE_ALTERNATE);
}

void i2c1_init(void) {
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC_APB1ENR1 |= RCC_APB1ENR1_I2C1EN;

    pin_to_i2c(I2C1_SCL_PIN);
    pin_to_i2c(I2C1_SDA_PIN);

    /* TIMINGR may only be written while the peripheral is disabled. */
    I2C1_CR1 &= ~I2C1_CR1_PE;
    I2C1_TIMINGR = (I2C1_TIMING_PRESC << I2C1_TIMING_PRESC_SHIFT)
                   | (I2C1_TIMING_SCLDEL << I2C1_TIMING_SCLDEL_SHIFT)
                   | (I2C1_TIMING_SDADEL << I2C1_TIMING_SDADEL_SHIFT)
                   | (I2C1_TIMING_SCLH << I2C1_TIMING_SCLH_SHIFT)
                   | I2C1_TIMING_SCLL;
    I2C1_CR1 |= I2C1_CR1_PE;
}

/**
 * Wait for any of `flags`, or for an error, or for the clock to run out.
 *
 * Returns the status, and on anything but ::I2C_OK the caller must stop
 * the transfer. Errors are cleared here so the next transfer starts from a
 * clean status register: a BERR left standing would be read as this
 * transfer's error by the one after it.
 */
static i2c_status_t wait_for(uint32_t flags) {
    const uint32_t started = core_millis();

    for (;;) {
        const uint32_t status = I2C1_ISR;

        if ((status & I2C1_ISR_NACKF) != 0U) {
            I2C1_ICR = I2C1_ICR_NACKCF;
            return I2C_NACK;
        }
        if ((status & I2C1_ISR_ANY_ERROR) != 0U) {
            I2C1_ICR = I2C1_ICR_ANY_ERROR;
            return I2C_BUS_ERROR;
        }
        if ((status & flags) != 0U) {
            return I2C_OK;
        }
        if ((core_millis() - started) >= I2C1_TIMEOUT_MS) {
            return I2C_TIMEOUT;
        }
    }
}

/**
 * Start one transfer: address, direction, length, and whether the
 * peripheral should place the stop itself.
 *
 * `auto_end` is the difference between a transfer that finishes on its own
 * and one that holds the bus for a repeated start.
 */
static void begin_transfer(uint8_t address7, uint8_t count, uint32_t read,
                           uint32_t auto_end) {
    I2C1_CR2 = ((uint32_t)address7 << I2C1_CR2_SADD_SHIFT)
               | ((uint32_t)count << I2C1_CR2_NBYTES_SHIFT)
               | read | auto_end | I2C1_CR2_START;
}

/// Put a stop on the bus and clear the flag it raises, whatever went wrong.
static void force_stop(void) {
    I2C1_CR2 |= I2C1_CR2_STOP;
    (void)wait_for(I2C1_ISR_STOPF);
    I2C1_ICR = I2C1_ICR_STOPCF;
}

/// Push `count` bytes once a transfer is already running.
static i2c_status_t send_bytes(const uint8_t *bytes, uint8_t count) {
    for (uint8_t i = 0U; i < count; i++) {
        const i2c_status_t status = wait_for(I2C1_ISR_TXIS);
        if (status != I2C_OK) {
            return status;
        }
        I2C1_TXDR = bytes[i];
    }
    return I2C_OK;
}

/// Pull `count` bytes once a read transfer is already running.
static i2c_status_t take_bytes(uint8_t *out, uint8_t count) {
    for (uint8_t i = 0U; i < count; i++) {
        const i2c_status_t status = wait_for(I2C1_ISR_RXNE);
        if (status != I2C_OK) {
            return status;
        }
        out[i] = (uint8_t)I2C1_RXDR;
    }
    return I2C_OK;
}

i2c_status_t i2c1_write(uint8_t address7, const uint8_t *bytes,
                        uint8_t count) {
    if (bytes == NULL) {
        return I2C_BUS_ERROR;
    }

    begin_transfer(address7, count, 0U, I2C1_CR2_AUTOEND);

    i2c_status_t status = send_bytes(bytes, count);
    if (status != I2C_OK) {
        force_stop();
        return status;
    }

    status = wait_for(I2C1_ISR_STOPF);
    I2C1_ICR = I2C1_ICR_STOPCF;
    return status;
}

i2c_status_t i2c1_read(uint8_t address7, uint8_t *out, uint8_t count) {
    if (out == NULL) {
        return I2C_BUS_ERROR;
    }

    begin_transfer(address7, count, I2C1_CR2_RD_WRN, I2C1_CR2_AUTOEND);

    i2c_status_t status = take_bytes(out, count);
    if (status != I2C_OK) {
        force_stop();
        return status;
    }

    status = wait_for(I2C1_ISR_STOPF);
    I2C1_ICR = I2C1_ICR_STOPCF;
    return status;
}

i2c_status_t i2c1_write_then_read(uint8_t address7, const uint8_t *tx,
                                  uint8_t tx_count, uint8_t *rx,
                                  uint8_t rx_count) {
    if ((tx == NULL) || (rx == NULL)) {
        return I2C_BUS_ERROR;
    }

    /* No AUTOEND on the write half: the peripheral must hold the bus so the
     * read can open with a repeated start. Transfer complete, not stop, is
     * what says the write half is done. */
    begin_transfer(address7, tx_count, 0U, 0U);

    i2c_status_t status = send_bytes(tx, tx_count);
    if (status != I2C_OK) {
        force_stop();
        return status;
    }

    status = wait_for(I2C1_ISR_TC);
    if (status != I2C_OK) {
        force_stop();
        return status;
    }

    begin_transfer(address7, rx_count, I2C1_CR2_RD_WRN, I2C1_CR2_AUTOEND);

    status = take_bytes(rx, rx_count);
    if (status != I2C_OK) {
        force_stop();
        return status;
    }

    status = wait_for(I2C1_ISR_STOPF);
    I2C1_ICR = I2C1_ICR_STOPCF;
    return status;
}
