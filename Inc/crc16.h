#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file crc16.h
 * @brief CRC-16/CCITT-FALSE, the per-frame checksum of the UART flash protocol.
 *
 * The protocol (industrial-hmi `docs/protocols/uart-flash-v1.md`, section 3)
 * names the variant exactly: polynomial 0x1021, initial value 0xFFFF, no
 * reflection, no final XOR. There are several CRC-16s with the same
 * polynomial and different details, and picking the wrong one produces a
 * checksum that looks fine and matches nothing. The way to prove the right one
 * was picked is its standard check value: over the ASCII string "123456789"
 * it must give 0x29B1. The host test asserts exactly that.
 *
 * Pure logic, no registers, so the same code runs on the board and on a PC.
 */

/// Starting value for a fresh checksum.
#define CRC16_INIT 0xFFFFU

/// Folds one more byte into a running checksum.
uint16_t crc16_update(uint16_t crc, uint8_t byte);

/// Checksum of a whole buffer, starting from ::CRC16_INIT.
uint16_t crc16_compute(const uint8_t *data, size_t len);

#endif /* CRC16_H */
