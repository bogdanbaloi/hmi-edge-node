/**
 * @file crc16.c
 * @brief CRC-16/CCITT-FALSE, bit by bit.
 *
 * Bitwise rather than table driven on purpose. A table costs 512 bytes of
 * flash to save a few microseconds per byte, and at 115200 baud a byte arrives
 * every 87 microseconds, so the loop below is nowhere near the limit.
 */

#include "crc16.h"

#define CRC16_POLY    0x1021U
#define CRC16_TOP_BIT 0x8000U

/// Bits processed per input byte.
#define BITS_PER_BYTE 8U

/// Shift that lines a byte up with the top of the 16-bit register. This
/// variant is not reflected, so each byte enters most significant bit first.
#define BYTE_TO_TOP 8U

uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    uint16_t c = (uint16_t)(crc ^ (uint16_t)((uint16_t)byte << BYTE_TO_TOP));
    for (uint32_t bit = 0U; bit < BITS_PER_BYTE; bit++) {
        if ((c & CRC16_TOP_BIT) != 0U) {
            c = (uint16_t)((uint16_t)(c << 1) ^ CRC16_POLY);
        } else {
            c = (uint16_t)(c << 1);
        }
    }
    return c;
}

uint16_t crc16_compute(const uint8_t *data, size_t len) {
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0U; i < len; i++) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}
