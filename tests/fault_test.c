/**
 * @file fault_test.c
 * @brief Unit test for the fault blink pattern.
 *
 * A third binary, and a third question. The other two ask "does the wire
 * format still match" and "is the number right". This one asks "can a person
 * actually tell these patterns apart", which is the only thing that makes a
 * blink code worth having.
 *
 * Only the pure part is testable. Whether the LED physically lights is a
 * hardware question answered by the board, and whether the vector table points
 * here is answered by objdump on the linked image. What a host test CAN check
 * is the property the whole idea rests on: four faults, four distinguishable
 * counts, each small enough to count by eye.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "fault.h"

#include <stdint.h>
#include <stdio.h>

/* fault.c also defines the four fault vectors, and those call into `board` to
   blink. The linker needs those symbols even though no test ever reaches them,
   so they are stubbed here rather than linking board.c, which exists only to
   write to fixed register addresses and has no meaning on a PC.
   If a test ever did call one, it would do nothing, which is the honest
   behaviour for hardware that is not present. */
void board_init(void);
void board_led_set(uint32_t on);

void board_init(void) { }
void board_led_set(uint32_t on) { (void)on; }

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

/// Every kind this firmware can hit. Kept as a table so a new fault vector
/// added later has to be added here too, and the properties below still hold.
static const fault_kind_t k_all[] = {
    FAULT_HARD, FAULT_MEMMANAGE, FAULT_BUS, FAULT_USAGE
};
#define KIND_COUNT (sizeof(k_all) / sizeof(k_all[0]))

/// The property the blink code lives or dies by. Two faults that blink the
/// same number of times carry no more information than a dead board.
static void every_fault_has_its_own_count(void) {
    for (size_t i = 0U; i < KIND_COUNT; i++) {
        for (size_t j = i + 1U; j < KIND_COUNT; j++) {
            CHECK(fault_blink_count(k_all[i]) != fault_blink_count(k_all[j]));
        }
    }
}

/// A single blink reads like a board flickering on reset, so nothing starts
/// at one. And a person counting from across the room gives up well before
/// ten, so nothing is allowed to run long either.
static void counts_are_countable_by_eye(void) {
    for (size_t i = 0U; i < KIND_COUNT; i++) {
        const uint32_t n = fault_blink_count(k_all[i]);
        CHECK(n >= 2U);
        CHECK(n <= 9U);
    }
}

/// The documented mapping, pinned. If these move, the comment in fault.h and
/// the README both become wrong, and a blink code you cannot look up is just
/// a blinking light.
static void the_documented_mapping_holds(void) {
    CHECK(fault_blink_count(FAULT_HARD) == 2U);
    CHECK(fault_blink_count(FAULT_MEMMANAGE) == 3U);
    CHECK(fault_blink_count(FAULT_BUS) == 4U);
    CHECK(fault_blink_count(FAULT_USAGE) == 5U);
}

int main(void) {
    (void)printf("unit: fault blink patterns\n");

    every_fault_has_its_own_count();
    counts_are_countable_by_eye();
    the_documented_mapping_holds();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
