#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file uart.h
 * @brief USART2 driver (the ST-Link virtual COM port): transmit and receive.
 *
 * Peripheral driver: the only place USART2 registers are touched. The
 * application talks in strings and bytes and never sees a register.
 *
 * Receiving is interrupt driven, sending is not. A received byte must be taken
 * within one byte time, 87 us at 115200, or the next one overwrites it, and
 * the main loop cannot promise that: a telemetry line alone keeps it busy for
 * about 2.7 ms. So the interrupt moves each byte into a queue and the loop
 * collects them with uart_read_byte() whenever it gets there. Sending has no
 * such deadline, so it stays a simple blocking loop.
 */

/// Returned by uart_read_byte() when a byte was taken.
#define UART_BYTE_READ 1U
/// Returned by uart_read_byte() when nothing is waiting.
#define UART_NOTHING_WAITING 0U

/// Initialise USART2 for 115200 8N1, transmit and receive, and enable its
/// receive interrupt. Assumes the port clock and the TX and RX pin alternate
/// functions are already configured (see board_init()).
void uart_init(void);

/// Send a NUL-terminated string, blocking until each byte is queued.
void uart_send_string(const char *text);

/// Send len bytes as they are, 0x00 included, and return only once the last
/// bit has left the pin. For binary frames, which uart_send_string() would cut
/// at the first 0x00, and for answers followed immediately by a reset: the
/// board switches banks right after the ACK to COMMIT, and a reset one byte
/// too early turns a successful update into a broken frame.
void uart_send_bytes(const uint8_t *bytes, size_t len);

/**
 * @brief Take the oldest received byte, if any. Never blocks.
 *
 * A byte is lost only if the queue is full or the interrupt itself came too
 * late; either leaves a gap in a frame, which the frame CRC catches and the
 * host answers by resending.
 *
 * @return ::UART_BYTE_READ with the byte in out, or ::UART_NOTHING_WAITING.
 */
uint32_t uart_read_byte(uint8_t *out);

#endif /* UART_H */
