/**
 * @file crc32.c
 * @brief CRC-32/ISO-HDLC. See crc32.h for the variant and why it is a table.
 */

#include "crc32.h"

/// The reflected polynomial of CRC-32/ISO-HDLC, pinned in the spec.
#define CRC32_POLYNOMIAL 0xEDB88320UL
/// Both the initial value and the final XOR of this variant.
#define CRC32_ALL_ONES 0xFFFFFFFFUL
/// One entry per possible byte.
#define CRC32_TABLE_SIZE 256U
/// Bits in the byte each table entry is built from.
#define CRC32_BITS_PER_BYTE 8U
/// Keeps an intermediate value inside one byte.
#define CRC32_BYTE_MASK 0xFFUL

static uint32_t g_table[CRC32_TABLE_SIZE];

void crc32_init(void) {
    for (uint32_t value = 0U; value < CRC32_TABLE_SIZE; value++) {
        uint32_t entry = value;
        for (uint32_t bit = 0U; bit < CRC32_BITS_PER_BYTE; bit++) {
            /* Reflected form: the low bit decides, and the shift goes right. */
            entry = ((entry & 1U) != 0U) ? ((entry >> 1U) ^ CRC32_POLYNOMIAL)
                                         : (entry >> 1U);
        }
        g_table[value] = entry;
    }
}

uint32_t crc32_compute(const uint8_t *bytes, size_t len) {
    uint32_t crc = CRC32_ALL_ONES;
    for (size_t i = 0U; i < len; i++) {
        const uint32_t index = (crc ^ (uint32_t)bytes[i]) & CRC32_BYTE_MASK;
        crc = g_table[index] ^ (crc >> CRC32_BITS_PER_BYTE);
    }
    return crc ^ CRC32_ALL_ONES;
}
