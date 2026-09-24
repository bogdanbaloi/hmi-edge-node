/**
 * @file option_plan_test.c
 * @brief The decision behind the one irreversible write on this chip.
 *
 * `RDP` level 2 locks the chip forever, and it lives in the same word as the
 * bank switch bit. So the rules under test are: refuse unless read protection
 * is level 0, change exactly one bit, and carry every other bit across
 * untouched, `RDP` above all.
 *
 * Values from RM0351 Rev 9: `RDP` is bits 7:0 with `0xAA` level 0 and `0xCC`
 * level 2, `BFB2` is bit 20.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "option_plan.h"

#include <stdint.h>
#include <stdio.h>

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

/// RDP level 0, the only level where anything may be written.
#define RDP_0 0xAAUL
/// RDP level 2, the one that cannot be undone.
#define RDP_2 0xCCUL
/// One of the many values that mean level 1.
#define RDP_1 0x55UL
/// BFB2, bit 20 of FLASH_OPTR.
#define BFB2 (1UL << 20)
/// Bits that have nothing to do with this decision and must survive it: the
/// watchdog options, the brown-out level, the boot configuration.
#define OTHER_BITS 0x3FEFFF00UL
/// A value that looks like a real chip: level 0, booting bank 1, other bits set.
#define OPTR_BANK_1 (RDP_0 | OTHER_BITS)
/// The same, booting bank 2.
#define OPTR_BANK_2 (RDP_0 | OTHER_BITS | BFB2)
/// Not a bank number.
#define NOT_A_BANK 3U
/// What out_optr holds before a call that must not touch it.
#define UNTOUCHED 0xDEADBEEFUL

static void the_three_read_protection_levels(void) {
    CHECK(option_plan_rdp(RDP_0) == OPTION_RDP_LEVEL_0);
    CHECK(option_plan_rdp(RDP_2) == OPTION_RDP_LEVEL_2);
    CHECK(option_plan_rdp(RDP_1) == OPTION_RDP_LEVEL_1);
    /* Anything that is neither 0xAA nor 0xCC is level 1, not "unknown". */
    CHECK(option_plan_rdp(0x00UL) == OPTION_RDP_LEVEL_1);
    CHECK(option_plan_rdp(0xFFUL) == OPTION_RDP_LEVEL_1);
    /* The level is read from the low byte only, whatever else is set. */
    CHECK(option_plan_rdp(OPTR_BANK_2) == OPTION_RDP_LEVEL_0);
}

static void which_bank_boots(void) {
    CHECK(option_plan_boot_bank(OPTR_BANK_1) == OPTION_BANK_1);
    CHECK(option_plan_boot_bank(OPTR_BANK_2) == OPTION_BANK_2);
}

/// The whole point: one bit changes, the rest of the word is carried across.
/// A plan that rebuilt the word could quietly move RDP, and RDP has a value
/// that cannot be taken back.
static void switching_changes_exactly_one_bit(void) {
    uint32_t planned = UNTOUCHED;
    CHECK(option_plan_for_bank(OPTR_BANK_1, OPTION_BANK_2, &planned) ==
          OPTION_PLAN_OK);
    CHECK(planned == (OPTR_BANK_1 | BFB2));
    CHECK((planned ^ OPTR_BANK_1) == BFB2);
    CHECK((planned & 0xFFUL) == RDP_0);

    planned = UNTOUCHED;
    CHECK(option_plan_for_bank(OPTR_BANK_2, OPTION_BANK_1, &planned) ==
          OPTION_PLAN_OK);
    CHECK((planned ^ OPTR_BANK_2) == BFB2);
    CHECK((planned & 0xFFUL) == RDP_0);
}

static void asking_for_the_bank_that_already_boots_writes_nothing(void) {
    uint32_t planned = UNTOUCHED;
    CHECK(option_plan_for_bank(OPTR_BANK_1, OPTION_BANK_1, &planned) ==
          OPTION_PLAN_ALREADY);
    CHECK(planned == UNTOUCHED);
    CHECK(option_plan_for_bank(OPTR_BANK_2, OPTION_BANK_2, &planned) ==
          OPTION_PLAN_ALREADY);
    CHECK(planned == UNTOUCHED);
}

/// At level 1 a write is the wrong move, and at level 2 there is nothing to
/// be done at all. Both refuse, and both leave the output alone.
static void read_protection_above_zero_refuses(void) {
    uint32_t planned = UNTOUCHED;
    CHECK(option_plan_for_bank(RDP_1 | OTHER_BITS, OPTION_BANK_2, &planned) ==
          OPTION_PLAN_REFUSED_RDP);
    CHECK(planned == UNTOUCHED);
    CHECK(option_plan_for_bank(RDP_2 | OTHER_BITS, OPTION_BANK_2, &planned) ==
          OPTION_PLAN_REFUSED_RDP);
    CHECK(planned == UNTOUCHED);
    /* Even the harmless-looking case is refused, because reading the state of
       a protected chip is not the same as being allowed to write it. */
    CHECK(option_plan_for_bank(RDP_2 | OTHER_BITS, OPTION_BANK_1, &planned) ==
          OPTION_PLAN_REFUSED_RDP);
    CHECK(planned == UNTOUCHED);
}

static void a_bank_that_does_not_exist_refuses(void) {
    uint32_t planned = UNTOUCHED;
    CHECK(option_plan_for_bank(OPTR_BANK_1, NOT_A_BANK, &planned) ==
          OPTION_PLAN_REFUSED_BANK);
    CHECK(planned == UNTOUCHED);
    CHECK(option_plan_for_bank(OPTR_BANK_1, 0U, &planned) ==
          OPTION_PLAN_REFUSED_BANK);
    CHECK(planned == UNTOUCHED);
}

/// The bank check comes first, so a nonsense bank is refused as a nonsense
/// bank even on a protected chip, rather than being blamed on RDP.
static void every_single_other_bit_survives_a_switch(void) {
    for (uint32_t bit = 8U; bit < 32U; bit++) {
        if (bit == 20U) {
            continue;  /* BFB2 itself, the one bit allowed to move */
        }
        const uint32_t optr = RDP_0 | (1UL << bit);
        uint32_t planned = UNTOUCHED;
        CHECK(option_plan_for_bank(optr, OPTION_BANK_2, &planned) ==
              OPTION_PLAN_OK);
        CHECK((planned & (1UL << bit)) != 0U);
        CHECK((planned & 0xFFUL) == RDP_0);
    }
}

int main(void) {
    (void)printf("unit: the option byte plan, the one irreversible write\n");

    the_three_read_protection_levels();
    which_bank_boots();
    switching_changes_exactly_one_bit();
    asking_for_the_bank_that_already_boots_writes_nothing();
    read_protection_above_zero_refuses();
    a_bank_that_does_not_exist_refuses();
    every_single_other_bit_survives_a_switch();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
