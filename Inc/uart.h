#ifndef UART_H
#define UART_H

/**
 * @file uart.h
 * @brief USART2 transmit driver (the ST-Link virtual COM port).
 *
 * Peripheral driver: the only place USART2 registers are touched. The
 * application talks in strings and never sees a register.
 */

/// Initialise USART2 for 115200 8N1 transmit. Assumes the port clock and the
/// TX pin alternate function are already configured (see board_init()).
void uart_init(void);

/// Send a NUL-terminated string, blocking until each byte is queued.
void uart_send_string(const char *text);

#endif /* UART_H */
