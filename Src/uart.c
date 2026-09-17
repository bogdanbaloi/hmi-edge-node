/**
 * @file uart.c
 * @brief USART2 transmit driver. The only place USART2 registers are touched.
 */

#include "uart.h"
#include "registers.h"

/// 4 MHz PCLK1 / 115200, oversampling 16 -> BRR = round(4e6 / 115200) = 35.
#define UART_BRR_115200 35UL

void uart_init(void) {
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    USART2_BRR = UART_BRR_115200;
    USART2_CR1 = USART2_CR1_TE | USART2_CR1_UE;  /* 8N1 is the reset default */
}

static void uart_send_char(char c) {
    while ((USART2_ISR & USART2_ISR_TXE) == 0U) {
    }
    USART2_TDR = (uint32_t)(uint8_t)c;
}

void uart_send_string(const char *text) {
    while (*text != '\0') {
        uart_send_char(*text++);
    }
}
