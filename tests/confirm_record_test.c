/**
 * @file confirm_record_test.c
 * @brief The CONFIRMED record: what counts as a confirmation and what does not.
 *
 * The rule under test is the one the protocol rests on (uart-flash-v1.md,
 * section 9, answer 4): no record means unconfirmed. So the interesting cases
 * are all the ways flash can look when nothing valid was written, and they
 * must every one of them read as "not confirmed".
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "confirm_record.h"

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

/// Not confirmed, as the function reports it.
#define NOT_CONFIRMED 0U
/// Confirmed.
#define CONFIRMED 1U
/// The checksum this test pretends the running image has.
#define THIS_IMAGE 0x1234ABCDU
/// A different image, one bit away: another build of the same firmware.
#define OTHER_IMAGE 0x1234ABCEU
/// What an erased flash byte reads as.
#define ERASED_BYTE 0xFFU
/// A page written to all zeros, the other degenerate case.
#define ZEROED_BYTE 0x00U

static void a_record_built_here_confirms_this_image(void) {
    uint8_t record[CONFIRM_RECORD_BYTES];
    confirm_record_build(record, THIS_IMAGE);
    CHECK(confirm_record_confirms(record, THIS_IMAGE) == CONFIRMED);
}

/// The case that matters most: a board that was never confirmed, or one whose
/// record page was just erased with the bank, must read as on trial.
static void erased_flash_confirms_nothing(void) {
    uint8_t erased[CONFIRM_RECORD_BYTES];
    memset(erased, ERASED_BYTE, sizeof erased);
    CHECK(confirm_record_confirms(erased, THIS_IMAGE) == NOT_CONFIRMED);
}

/// All zeros is not "written", it is a different kind of nothing.
static void a_zeroed_page_confirms_nothing(void) {
    uint8_t zeroed[CONFIRM_RECORD_BYTES];
    memset(zeroed, ZEROED_BYTE, sizeof zeroed);
    CHECK(confirm_record_confirms(zeroed, THIS_IMAGE) == NOT_CONFIRMED);
}

/// A record from the image that ran before this one must not confirm this
/// one: the new image would skip its trial on the strength of an old promise.
/// This is the bug the review found on 2026-09-22, when the identity was a
/// version constant that every build shared.
static void another_image_confirms_nothing(void) {
    uint8_t record[CONFIRM_RECORD_BYTES];
    confirm_record_build(record, OTHER_IMAGE);
    CHECK(confirm_record_confirms(record, THIS_IMAGE) == NOT_CONFIRMED);
}

/// Any single byte of the marker or the version corrupted, and it is void.
/// A power cut in the middle of the one double word is exactly this.
static void one_wrong_byte_anywhere_voids_it(void) {
    for (uint32_t at = 0U; at < CONFIRM_RECORD_BYTES; at++) {
        uint8_t record[CONFIRM_RECORD_BYTES];
        confirm_record_build(record, THIS_IMAGE);
        record[at] = (uint8_t)(record[at] ^ 1U);
        CHECK(confirm_record_confirms(record, THIS_IMAGE) == NOT_CONFIRMED);
    }
}

/// The identity really is stored, not implied: two images give two records.
static void the_identity_is_part_of_the_record(void) {
    uint8_t mine[CONFIRM_RECORD_BYTES];
    uint8_t other[CONFIRM_RECORD_BYTES];
    confirm_record_build(mine, THIS_IMAGE);
    confirm_record_build(other, OTHER_IMAGE);
    CHECK(memcmp(mine, other, CONFIRM_RECORD_BYTES) != 0);
    CHECK(confirm_record_confirms(other, OTHER_IMAGE) == CONFIRMED);
}

int main(void) {
    (void)printf("unit: the CONFIRMED record in flash\n");

    a_record_built_here_confirms_this_image();
    erased_flash_confirms_nothing();
    a_zeroed_page_confirms_nothing();
    another_image_confirms_nothing();
    one_wrong_byte_anywhere_voids_it();
    the_identity_is_part_of_the_record();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
