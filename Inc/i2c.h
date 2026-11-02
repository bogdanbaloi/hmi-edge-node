#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/**
 * @file i2c.h
 * @brief I2C1 on PB8 and PB9, register level. HAL: the only place the I2C1
 *        and GPIOB registers are touched.
 *
 * Knows nothing about any device on the bus. The ADXL345 driver is built on
 * top of it through function pointers, so that driver can be tested on a PC
 * against a fake bus while this file is only ever proved on the board.
 *
 * **Every call has a timeout and none of them can spin forever.** That is
 * the whole point of this file rather than a nicety: a sensor that is not
 * plugged in holds nothing, a sensor wired wrong can hold SCL low, and a
 * main loop that waits for a flag which will never arrive takes the
 * telemetry and the firmware update down with it. The watchdog would reboot
 * the board every 32 seconds, which is a worse failure than reporting that
 * the sensor is absent.
 *
 * **What the addresses and the bit positions come from:** `STM32L476.svd`
 * in the STM32CubeIDE install, which the delivery checklist names as the
 * source for exactly this. Base 0x40005400, the register offsets and every
 * field were read from it rather than remembered.
 *
 * **What they do NOT come from, said plainly:** RM0351. st.com could not be
 * reached from this machine on 2026-11-02, so the TRANSFER SEQUENCE and the
 * TIMINGR arithmetic below are written from knowledge of this peripheral
 * family rather than from a page I read. They are a hypothesis with a cheap
 * test: the ADXL345 answers 0xE5 to a read of register 0x00, so the first
 * successful DEVID read is what turns this from plausible into proved.
 */

/// How a transfer ended. Anything other than ::I2C_OK leaves the bus idle.
typedef enum {
    I2C_OK = 0,      ///< Every byte moved and a stop was seen.
    I2C_TIMEOUT,     ///< A flag did not arrive in time. Device absent or stuck.
    I2C_NACK,        ///< The device did not acknowledge its address or a byte.
    I2C_BUS_ERROR    ///< Arbitration lost or a misplaced start or stop.
} i2c_status_t;

/**
 * @brief Bring up I2C1 at 100 kHz on PB8 (SCL) and PB9 (SDA).
 *
 * Call once, after the clocks are up. Both pins go to alternate function 4,
 * open drain, with the internal pull-ups on. A real board usually carries
 * external pull-ups as well, and the two in parallel are harmless.
 */
void i2c1_init(void);

/// Write `count` bytes to `address7`. Sends start, the bytes, then stop.
i2c_status_t i2c1_write(uint8_t address7, const uint8_t *bytes, uint8_t count);

/// Read `count` bytes from `address7`. Sends start, reads, then stop.
i2c_status_t i2c1_read(uint8_t address7, uint8_t *out, uint8_t count);

/**
 * @brief Write, then read without releasing the bus. The shape a register
 *        read needs: name the register, repeated start, take the answer.
 *
 * Splitting this into a write and a read would let another master, or a
 * stray stop, land between the two halves, and the device would answer from
 * the wrong register.
 */
i2c_status_t i2c1_write_then_read(uint8_t address7, const uint8_t *tx,
                                  uint8_t tx_count, uint8_t *rx,
                                  uint8_t rx_count);

#endif /* I2C_H */
