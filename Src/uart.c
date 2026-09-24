/**
 * @file uart.c
 * @brief USART2 driver. The only place USART2 registers are touched.
 */

#include "uart.h"
#include "byte_ring.h"
#include "registers.h"

/// 4 MHz PCLK1 / 115200, oversampling 16 -> BRR = round(4e6 / 115200) = 35.
#define UART_BRR_115200 35UL

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

static void uart_send_char(uint8_t byte) {
    while ((USART2_ISR & USART2_ISR_TXE) == 0U) {
    }
    USART2_TDR = (uint32_t)byte;
}

void uart_send_string(const char *text) {
    while (*text != '\0') {
        uart_send_char((uint8_t)*text++);
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
 */
void uart_send_bytes(const uint8_t *bytes, size_t len) {
    for (size_t i = 0U; i < len; i++) {
        uart_send_char(bytes[i]);
    }
    while ((USART2_ISR & USART2_ISR_TC) == 0U) {
    }
}
