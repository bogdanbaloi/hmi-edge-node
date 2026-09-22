/**
 * @file crc_unit.c
 * @brief The CRC peripheral as CRC-32/ISO-HDLC. See crc_unit.h for why.
 */

#include "crc_unit.h"
#include "registers.h"

#include <stdint.h>

/// The final XOR of this variant, which the hardware does not do.
#define CRC_UNIT_FINAL_XOR 0xFFFFFFFFUL
/// Bytes fed per word write.
#define CRC_UNIT_WORD_BYTES 4U
/// Bits per byte, for taking bytes out of a word.
#define CRC_UNIT_BITS_PER_BYTE 8U
/// Keeps one byte of a word.
#define CRC_UNIT_BYTE_MASK 0xFFU
/// The catalogue check string of CRC-32, "123456789", and its value: the same
/// anchor the host test uses (see tests/crc32_test.c). It is here as bytes AND
/// as words, because the two take different paths through the peripheral and
/// both are checked at start-up.
#define CRC_UNIT_CHECK_LENGTH 9U
#define CRC_UNIT_CHECK_VALUE 0xCBF43926UL
static const uint8_t k_check_bytes[CRC_UNIT_CHECK_LENGTH] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
};
/// "12345678" as two little-endian words, then a word holding only the '9'.
static const uint32_t k_check_words[3] = { 0x34333231UL, 0x38373635UL,
                                           0x00000039UL };

static uint32_t g_trustworthy;

static void reset_unit(void) {
    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT | CRC_CR_RESET;
}

static uint32_t result(void) {
    return CRC_DR ^ CRC_UNIT_FINAL_XOR;
}

void crc_unit_init(void) {
    RCC_AHB1ENR |= RCC_AHB1ENR_CRCEN;
    /* Polynomial and initial value are already right at reset. Reversing the
       input by byte and the output is what turns the hardware's arrangement
       into the reflected CRC-32/ISO-HDLC the protocol pins. */
    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT;

    const uint32_t as_bytes = crc_unit_compute(&k_check_bytes[0],
                                               CRC_UNIT_CHECK_LENGTH);
    const uint32_t as_words = crc_unit_compute_words(&k_check_words[0],
                                                     CRC_UNIT_CHECK_LENGTH);
    g_trustworthy = ((as_bytes == CRC_UNIT_CHECK_VALUE) &&
                     (as_words == CRC_UNIT_CHECK_VALUE))
                        ? 1U
                        : 0U;
}

uint32_t crc_unit_is_trustworthy(void) {
    return g_trustworthy;
}

uint32_t crc_unit_compute(const uint8_t *bytes, size_t len) {
    reset_unit();
    for (size_t i = 0U; i < len; i++) {
        CRC_DR_BYTE = bytes[i];
    }
    return result();
}

uint32_t crc_unit_compute_words(const uint32_t *words, size_t len) {
    const size_t whole = len / CRC_UNIT_WORD_BYTES;
    const size_t tail = len % CRC_UNIT_WORD_BYTES;

    reset_unit();
    /* Reversing by word instead of by byte lets four bytes go in one write and
       gives the same answer, which is why the mode changes mid-computation. */
    CRC_CR = CRC_CR_REV_IN_WORD | CRC_CR_REV_OUT;
    for (size_t i = 0U; i < whole; i++) {
        CRC_DR = words[i];
    }
    CRC_CR = CRC_CR_REV_IN_BYTE | CRC_CR_REV_OUT;

    /* The last 1 to 3 bytes sit in the low end of the next word, the memory
       being little-endian. Reading that word reads a few bytes past the image,
       which is inside the same bank and goes nowhere near the running one. */
    for (size_t i = 0U; i < tail; i++) {
        const uint32_t last = words[whole];
        CRC_DR_BYTE = (uint8_t)((last >> (CRC_UNIT_BITS_PER_BYTE * i)) &
                                CRC_UNIT_BYTE_MASK);
    }
    return result();
}
