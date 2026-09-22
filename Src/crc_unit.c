/**
 * @file crc_unit.c
 * @brief The CRC peripheral as CRC-32/ISO-HDLC. See crc_unit.h for why.
 */

#include "crc_unit.h"
#include "registers.h"

/// The final XOR of this variant, which the hardware does not do.
#define CRC_UNIT_FINAL_XOR 0xFFFFFFFFUL
/// Bytes fed per word write.
#define CRC_UNIT_WORD_BYTES 4U
/// Bits per byte, for assembling a word out of bytes.
#define CRC_UNIT_BITS_PER_BYTE 8U
/// The catalogue check string of CRC-32 and its value, the same anchor the
/// host test uses (see tests/crc32_test.c).
#define CRC_UNIT_CHECK_STRING "123456789"
#define CRC_UNIT_CHECK_LENGTH 9U
#define CRC_UNIT_CHECK_VALUE 0xCBF43926UL

static uint32_t g_trustworthy;

void crc_unit_init(void) {
    RCC_AHB1ENR |= RCC_AHB1ENR_CRCEN;
    /* Polynomial and initial value are already right at reset. Reversing the
       input by byte and the output is what turns the hardware's CRC-32/MPEG-2
       arrangement into the reflected CRC-32/ISO-HDLC the protocol pins. */
    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT;

    const uint8_t *check = (const uint8_t *)CRC_UNIT_CHECK_STRING;
    g_trustworthy =
        (crc_unit_compute(check, CRC_UNIT_CHECK_LENGTH) == CRC_UNIT_CHECK_VALUE)
            ? 1U
            : 0U;
}

uint32_t crc_unit_is_trustworthy(void) {
    return g_trustworthy;
}

/// Reversing by word instead of by byte lets four bytes go in one write and
/// gives the same result, which is why the mode changes mid-computation.
static void feed_words(const uint8_t *bytes, size_t words) {
    CRC_CR = CRC_CR_REV_IN_WORD | CRC_CR_REV_OUT;
    for (size_t i = 0U; i < words; i++) {
        const size_t at = i * CRC_UNIT_WORD_BYTES;
        CRC_DR = (uint32_t)bytes[at] |
                 ((uint32_t)bytes[at + 1U] << CRC_UNIT_BITS_PER_BYTE) |
                 ((uint32_t)bytes[at + 2U] << (2U * CRC_UNIT_BITS_PER_BYTE)) |
                 ((uint32_t)bytes[at + 3U] << (3U * CRC_UNIT_BITS_PER_BYTE));
    }
    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT;
}

uint32_t crc_unit_compute(const uint8_t *bytes, size_t len) {
    const size_t words = len / CRC_UNIT_WORD_BYTES;

    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT | CRC_CR_RESET;
    if (words > 0U) {
        feed_words(bytes, words);
    }
    for (size_t i = words * CRC_UNIT_WORD_BYTES; i < len; i++) {
        CRC_DR_BYTE = bytes[i];
    }
    return CRC_DR ^ CRC_UNIT_FINAL_XOR;
}
