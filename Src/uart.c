/**
 * @file uart.c
 * @brief USART2 driver. The only place USART2 registers are touched.
 */

#include "uart.h"
#include "byte_ring.h"
#include "registers.h"

/// 4 MHz PCLK1 / 115200, oversampling 16 -> BRR = round(4e6 / 115200) = 35.
#define UART_BRR_115200 35UL

/// How many turns a send waits for the transmitter before giving up on the
/// byte. The arithmetic behind the number is in wait_for_flag below.
#define UART_SEND_SPIN_LIMIT 100000UL

/// Received bytes on their way from the interrupt to the main loop. The
/// interrupt is its only writer and uart_read_byte() its only reader, which is
/// the condition byte_ring.h needs to be safe without masking interrupts.
static byte_ring_t g_rx;

void uart_init(void) {
    byte_ring_init(&g_rx);  /* before the interrupt can fire */
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    USART2_BRR = UART_BRR_115200;
    /* 8N1 is the reset default. */
    USART2_CR1 = USART2_CR1_TE | USART2_CR1_RE | USART2_CR1_RXNEIE |
                 USART2_CR1_UE;
    NVIC_ISER(IRQ_USART2) = NVIC_ISER_BIT(IRQ_USART2);
}

/**
 * Replaces the startup file's weak alias to Default_Handler, which is an
 * infinite loop, by having the identical name. The name is load-bearing: see
 * the `.*_Handler` exception in .clang-tidy.
 *
 * RXNEIE raises this interrupt for a received byte AND for an overrun, so both
 * must be cleared here, or the interrupt fires again the moment it returns and
 * the main loop never runs. Reading RDR clears RXNE. The error flags, overrun
 * above all, are cleared by writing them to ICR. A byte that arrived with a
 * framing or noise error is still passed on: judging it is the frame CRC's
 * job, not the driver's.
 */
void USART2_IRQHandler(void) {
    const uint32_t status = USART2_ISR;
    if ((status & USART2_ISR_RXNE) != 0U) {
        (void)byte_ring_push(&g_rx, (uint8_t)USART2_RDR);
    }
    if ((status & USART2_ICR_ERRORS) != 0U) {
        USART2_ICR = status & USART2_ICR_ERRORS;
    }
}

uint32_t uart_read_byte(uint8_t *out) {
    return byte_ring_pop(&g_rx, out) == BYTE_RING_MOVED ? UART_BYTE_READ
                                                       : UART_NOTHING_WAITING;
}

/**
 * @brief Wait for one flag in the status register, but not forever.
 *
 * Counted in SPINS, not milliseconds, and that is the point. A timeout built
 * on core_millis() would be useless here for two reasons: this is called
 * before core_tick_init() has started the clock on some paths, and a stopped
 * clock is one of the faults this guard exists to survive. A guard that
 * depends on the thing it guards against is not a guard.
 *
 * The number: one byte at 115200 takes 87 us, which is 348 cycles at the
 * 4 MHz this firmware runs at. This loop is a load, a mask and a branch, so
 * roughly fifty turns per byte time. A limit of 100000 is about two thousand
 * byte times, around 0.17 s. Far past any wait a working transmitter can
 * need, and far short of the 32.8 s watchdog, which is the whole point: the
 * loop gets to carry on and feed the dog instead of dying inside this
 * function.
 *
 * Returns 1 when the flag arrived, 0 when it ran out of turns.
 */
static uint32_t wait_for_flag(uint32_t flag) {
    for (uint32_t spins = 0U; spins < UART_SEND_SPIN_LIMIT; spins++) {
        if ((USART2_ISR & flag) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

/// Hand one byte to the transmitter. Returns 0 if it never asked for it,
/// in which case the transmitter is not working and the caller should stop.
static uint32_t uart_send_char(uint8_t byte) {
    if (!wait_for_flag(USART2_ISR_TXE)) {
        return 0U;
    }
    USART2_TDR = (uint32_t)byte;
    return 1U;
}

/**
 * Stops at the first byte the transmitter would not take.
 *
 * The rest of the line is abandoned on purpose. A transmitter that did not
 * ask for one byte will not ask for the next either, so pushing on would
 * spend the spin limit again per character and turn a dead UART into a stall
 * of seconds. This is a silent loss, which this firmware normally refuses,
 * but there is no honest way to report a broken UART over that same UART. The
 * board staying alive is what buys the chance to say it some other way.
 */
void uart_send_string(const char *text) {
    while (*text != '\0') {
        if (!uart_send_char((uint8_t)*text++)) {
            return;
        }
    }
}

/**
 * Waits for the last bit to leave the pin, not just for the data register to
 * be free. TXE means "hand me the next byte", TC means "the wire is idle".
 *
 * Found on the board on 2026-09-24: the ACK to COMMIT reached the host with 7
 * bytes instead of 8, because the answer is followed immediately by the reset
 * that switches banks, and the reset cut the last byte out of the shift
 * register. A successful update read as a broken frame.
 *
 * Both waits are bounded. The TC wait especially: it is the last thing that
 * runs before a bank switch, so a transmitter that never finishes would hang
 * the board at the exact moment an update is meant to complete.
 */
void uart_send_bytes(const uint8_t *bytes, size_t len) {
    for (size_t i = 0U; i < len; i++) {
        if (!uart_send_char(bytes[i])) {
            return;
        }
    }
    (void)wait_for_flag(USART2_ISR_TC);
}
