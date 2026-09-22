/**
 * @file crc32_test.c
 * @brief The image checksum of the update protocol, CRC-32/ISO-HDLC.
 *
 * The anchor is the variant's standard check value, `0xCBF43926` over the
 * ASCII string `123456789`. industrial-hmi reproduced the same value with
 * zlib's crc32() when the variant was pinned, so both sides are anchored to
 * the standard rather than to each other's code.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "crc32.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned g_checks;
static unsigned g_failures;

/// Record one boolean expectation, printing only on failure.
static void check(int condition, const char *what, int line) {
    g_checks++;
    if (!condition) {
        g_failures++;
        (void)printf("  FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// The standard check string of every CRC catalogue entry.
static const char k_check_string[] = "123456789";
/// CRC-32/ISO-HDLC over that string, the value the spec pins.
#define SPEC_CRC32_CHECK_VALUE 0xCBF43926UL
/// CRC-32/ISO-HDLC over no bytes at all: init XOR final, so zero.
#define CRC32_OF_NOTHING 0UL
/// A page's worth of bytes, enough to cross the table many times.
#define A_PAGE 2048U
/// Two arbitrary but fixed bytes for the "one bit apart" test.
#define BYTE_A 0x00U
#define BYTE_B 0x80U

static void the_standard_check_value(void) {
    const uint32_t crc = crc32_compute((const uint8_t *)k_check_string,
                                       strlen(k_check_string));
    CHECK(crc == SPEC_CRC32_CHECK_VALUE);
}

/// An image of zero bytes never reaches the CRC (BEGIN refuses size 0), but a
/// checksum function that mishandled an empty range would be hiding a bug.
static void nothing_has_a_defined_value(void) {
    const uint8_t nothing[1] = { 0U };
    CHECK(crc32_compute(nothing, 0U) == CRC32_OF_NOTHING);
}

/// Computed in one go or in two halves, an image must give the same answer
/// only if the CRC is taken over the whole range: this checks the caller's
/// assumption that the CRC is NOT resumable, which is why the board reads the
/// whole image back at COMMIT instead of accumulating as DATA arrives.
static void the_whole_range_is_what_counts(void) {
    uint8_t page[A_PAGE];
    for (uint32_t i = 0U; i < A_PAGE; i++) {
        page[i] = (uint8_t)(i & 0xFFU);
    }
    const uint32_t whole = crc32_compute(page, A_PAGE);
    const uint32_t first_half = crc32_compute(page, A_PAGE / 2U);
    CHECK(whole != first_half);
    CHECK(crc32_compute(page, A_PAGE) == whole);  /* and it is repeatable */
}

/// The property the whole verify step rests on: a flipped bit must change the
/// answer. A single byte differing in one bit is the smallest real corruption.
static void one_flipped_bit_changes_it(void) {
    uint8_t image[A_PAGE];
    memset(image, BYTE_A, sizeof image);
    const uint32_t clean = crc32_compute(image, sizeof image);

    for (uint32_t at = 0U; at < sizeof image; at += A_PAGE / 8U) {
        image[at] = BYTE_B;
        CHECK(crc32_compute(image, sizeof image) != clean);
        image[at] = BYTE_A;
    }
    CHECK(crc32_compute(image, sizeof image) == clean);
}

/// Trailing 0xFF bytes are what an erased flash page reads as, so an image
/// and the same image with erased padding behind it must differ.
static void erased_padding_is_not_part_of_the_image(void) {
    uint8_t image[16] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
                          0xFFU, 0xFFU, 0xFFU, 0xFFU,
                          0xFFU, 0xFFU, 0xFFU, 0xFFU };
    CHECK(crc32_compute(image, 8U) != crc32_compute(image, sizeof image));
}

int main(void) {
    (void)printf("unit: CRC-32/ISO-HDLC, the image checksum\n");
    crc32_init();

    the_standard_check_value();
    nothing_has_a_defined_value();
    the_whole_range_is_what_counts();
    one_flipped_bit_changes_it();
    erased_padding_is_not_part_of_the_image();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
