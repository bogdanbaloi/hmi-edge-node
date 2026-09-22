#ifndef LE_BYTES_H
#define LE_BYTES_H

#include <stdint.h>

/**
 * @file le_bytes.h
 * @brief Little-endian numbers in byte buffers.
 *
 * The UART flash protocol sends every number wider than a byte low byte first
 * (uart-flash-v1.md, section 3). The frame envelope needs 16-bit fields and
 * the update messages need 32-bit ones. Written once here, instead of once per
 * module that parses bytes, so the byte order has exactly one definition.
 *
 * Byte by byte on purpose, never a pointer cast: a payload field sits at any
 * offset, and a misaligned 32-bit load is undefined behaviour in C.
 */

#define LE_BYTE_MASK  0xFFU  ///< The low eight bits of a value.
#define LE_BYTE_SHIFT 8U     ///< Bits per byte, the step between bytes.

static inline uint16_t le_read16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] |
                      (uint16_t)((uint16_t)p[1] << LE_BYTE_SHIFT));
}

static inline void le_write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & LE_BYTE_MASK);
    p[1] = (uint8_t)(value >> LE_BYTE_SHIFT);
}

static inline uint32_t le_read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << LE_BYTE_SHIFT) |
           ((uint32_t)p[2] << (2U * LE_BYTE_SHIFT)) |
           ((uint32_t)p[3] << (3U * LE_BYTE_SHIFT));
}

static inline void le_write32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & LE_BYTE_MASK);
    p[1] = (uint8_t)((value >> LE_BYTE_SHIFT) & LE_BYTE_MASK);
    p[2] = (uint8_t)((value >> (2U * LE_BYTE_SHIFT)) & LE_BYTE_MASK);
    p[3] = (uint8_t)((value >> (3U * LE_BYTE_SHIFT)) & LE_BYTE_MASK);
}

#endif /* LE_BYTES_H */
