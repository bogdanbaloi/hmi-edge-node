/**
 * @file temperature_test.c
 * @brief Unit test for the counts-to-degrees conversion.
 *
 * Separate from the contract test on purpose. That one asks "does the wire
 * format match what industrial-hmi parses". This one asks "is the number
 * right", which is arithmetic, not protocol. Mixing them would blur what a
 * red build is telling you.
 *
 * The calibration constants used below are representative of a real
 * STM32L476RG: ST burns them per part, so the exact values differ from chip
 * to chip while the shape of the maths does not.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "temperature.h"

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

/// Record one integer expectation, showing both sides when they differ.
static void check_eq(int32_t actual, int32_t expected, int line) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        (void)printf("  FAIL line %d: expected %ld, got %ld\n", line,
                     (long)expected, (long)actual);
    }
}

/// Record an expectation with a tolerance, for cases where integer division
/// legitimately loses a tenth or two.
static void check_near(int32_t actual, int32_t expected, int32_t tolerance,
                       int line) {
    g_checks++;
    const int32_t delta = (actual > expected) ? (actual - expected)
                                             : (expected - actual);
    if (delta > tolerance) {
        g_failures++;
        (void)printf("  FAIL line %d: expected %ld +/- %ld, got %ld\n", line,
                     (long)expected, (long)tolerance, (long)actual);
    }
}

#define CHECK(cond)             check((cond), #cond, __LINE__)
#define CHECK_EQ(a, b)          check_eq((a), (b), __LINE__)
#define CHECK_NEAR(a, b, tol)   check_near((a), (b), (tol), __LINE__)

/* Representative factory constants for an L476RG. */
#define TS_CAL1_TYPICAL     1043U  /* raw at 30 C,  VDDA 3.0 V */
#define TS_CAL2_TYPICAL     1352U  /* raw at 130 C, VDDA 3.0 V */
#define VREFINT_CAL_TYPICAL 1655U  /* VREFINT raw,  VDDA 3.0 V */

/* VREFINT as actually measured on a Nucleo, whose VDDA is 3.3 V, not 3.0:
   1655 * 3.0 / 3.3. This is the whole reason the correction exists. */
#define VREFINT_AT_3V3 1505U

static const temperature_cal_t k_typical = {
    TS_CAL1_TYPICAL, TS_CAL2_TYPICAL, VREFINT_CAL_TYPICAL
};

/* ------------------------------------------------------------------ */

/// At the calibration VDDA, the two calibration points must come back exactly.
/// If these drift, the line itself is wrong, not just its scaling.
static void calibration_points_are_exact(void) {
    /* vrefint_raw == vrefint_cal means VDDA is exactly 3.0 V: no rescale. */
    CHECK_EQ(temperature_deci_celsius(&k_typical, TS_CAL1_TYPICAL,
                                      VREFINT_CAL_TYPICAL), 300);
    CHECK_EQ(temperature_deci_celsius(&k_typical, TS_CAL2_TYPICAL,
                                      VREFINT_CAL_TYPICAL), 1300);
}

/// Halfway between the calibration counts is halfway up the range.
static void midpoint_interpolates(void) {
    const uint32_t midpoint = (TS_CAL1_TYPICAL + TS_CAL2_TYPICAL) / 2U;
    CHECK_NEAR(temperature_deci_celsius(&k_typical, midpoint,
                                        VREFINT_CAL_TYPICAL),
               800, 10);  /* 80.0 C, allow a tenth of slack */
}

/// On a 3.3 V Nucleo the raw count for a 30 C die is smaller, but VREFINT is
/// smaller by the same ratio, so the answer still lands on 30 C.
static void vdda_correction_recovers_the_real_temperature(void) {
    /* The same die voltage read at 3.3 V instead of 3.0 V. */
    const uint32_t ts_at_3v3 = (TS_CAL1_TYPICAL * 3000U) / 3300U;

    CHECK_NEAR(temperature_deci_celsius(&k_typical, ts_at_3v3, VREFINT_AT_3V3),
               300, 10);
}

/**
 * The bug this correction exists to prevent, written down as a test.
 *
 * Skipping the VREFINT rescale means applying the 3.0 V calibration line
 * straight to a 3.3 V reading. The result is not slightly off, it is roughly
 * 30 degrees off, and it still looks like a plausible temperature. This case
 * asserts the size of that error so nobody "simplifies" the rescale away.
 */
static void skipping_the_correction_would_be_wildly_wrong(void) {
    const uint32_t ts_at_3v3 = (TS_CAL1_TYPICAL * 3000U) / 3300U;

    const int32_t correct =
        temperature_deci_celsius(&k_typical, ts_at_3v3, VREFINT_AT_3V3);

    /* What the naive formula would produce, inlined here so the comparison is
       visible rather than asserted on faith. */
    const int32_t span  = (int32_t)TS_CAL2_TYPICAL - (int32_t)TS_CAL1_TYPICAL;
    const int32_t naive =
        300 + ((1000 * ((int32_t)ts_at_3v3 - (int32_t)TS_CAL1_TYPICAL)) / span);

    CHECK_NEAR(correct, 300, 10);
    CHECK(naive < 100);              /* it reports near freezing... */
    CHECK((correct - naive) > 250);  /* ...a 25+ degree lie */
}

/// A warmer die must read warmer. The sensor slope is positive on the L476.
static void hotter_counts_read_hotter(void) {
    int32_t previous = TEMPERATURE_INVALID;

    for (uint32_t raw = TS_CAL1_TYPICAL; raw <= TS_CAL2_TYPICAL; raw += 10U) {
        const int32_t deci =
            temperature_deci_celsius(&k_typical, raw, VREFINT_CAL_TYPICAL);
        CHECK(deci != TEMPERATURE_INVALID);
        if (previous != TEMPERATURE_INVALID) {
            CHECK(deci > previous);
        }
        previous = deci;
    }
}

/// Negative temperatures are real: the die can sit below zero. The conversion
/// must produce them rather than wrapping or clamping.
static void below_zero_is_representable(void) {
    /* Well below the 30 C calibration point. */
    const int32_t deci =
        temperature_deci_celsius(&k_typical, TS_CAL1_TYPICAL - 100U,
                                 VREFINT_CAL_TYPICAL);
    CHECK(deci != TEMPERATURE_INVALID);
    CHECK(deci < 0);
}

/* ------------------------------------------------------------------ */
/* The guards. Each one stands between a degenerate input and either a       */
/* divide-by-zero or a confidently wrong number.                             */
/* ------------------------------------------------------------------ */

static void blank_flash_is_rejected(void) {
    const temperature_cal_t all_blank = { 0xFFFFU, 0xFFFFU, 0xFFFFU };
    CHECK(temperature_cal_usable(&all_blank) == 0U);
    CHECK_EQ(temperature_deci_celsius(&all_blank, 1000U, 1500U),
             TEMPERATURE_INVALID);

    /* One blank field is enough: a wrong address may hit only part of it. */
    const temperature_cal_t one_blank = { 0xFFFFU, TS_CAL2_TYPICAL,
                                          VREFINT_CAL_TYPICAL };
    CHECK(temperature_cal_usable(&one_blank) == 0U);
}

static void degenerate_calibration_is_rejected(void) {
    /* Zero reference: the rescale would divide by zero. */
    const temperature_cal_t no_vref = { TS_CAL1_TYPICAL, TS_CAL2_TYPICAL, 0U };
    CHECK(temperature_cal_usable(&no_vref) == 0U);

    /* Equal points: the line has no span, so it would divide by zero. */
    const temperature_cal_t flat = { 1200U, 1200U, VREFINT_CAL_TYPICAL };
    CHECK(temperature_cal_usable(&flat) == 0U);
    CHECK_EQ(temperature_deci_celsius(&flat, 1200U, VREFINT_CAL_TYPICAL),
             TEMPERATURE_INVALID);
}

static void degenerate_readings_are_rejected(void) {
    /* A dead reference reading would divide by zero. */
    CHECK_EQ(temperature_deci_celsius(&k_typical, 1000U, 0U),
             TEMPERATURE_INVALID);

    /* Neither reading can exceed 12-bit full scale. */
    CHECK_EQ(temperature_deci_celsius(&k_typical, 4096U, VREFINT_CAL_TYPICAL),
             TEMPERATURE_INVALID);
    CHECK_EQ(temperature_deci_celsius(&k_typical, 1000U, 4096U),
             TEMPERATURE_INVALID);

    /* A reference reading far below the calibration blows the corrected value
       past anything physical. Rejected rather than scaled into nonsense. */
    CHECK_EQ(temperature_deci_celsius(&k_typical, 4095U, 1U),
             TEMPERATURE_INVALID);
}

/// An implausible result is reported as no result, not as a number. Feeding a
/// reference reading far below calibration inflates the corrected count and
/// would otherwise yield hundreds of degrees.
static void implausible_results_are_rejected(void) {
    CHECK_EQ(temperature_deci_celsius(&k_typical, 4000U, 700U),
             TEMPERATURE_INVALID);
}

/**
 * Sweep the whole input domain for wrapped arithmetic.
 *
 * A bounds check alone would be circular here, since the function now clamps
 * its own output. Monotonicity is not: for a fixed reference, a higher count
 * must give a higher temperature, and the valid results must form ONE
 * contiguous run. Any overflow or wrap would break one or the other, whatever
 * range the wrapped value happened to land in.
 */
static void whole_input_domain_is_monotonic(void) {
    unsigned breaks = 0U;

    for (uint32_t vref = 1U; vref <= 4095U; vref += 13U) {
        int32_t  previous   = 0;
        unsigned seen_valid = 0U;
        unsigned run_ended  = 0U;

        for (uint32_t ts = 0U; ts <= 4095U; ts++) {
            const int32_t deci =
                temperature_deci_celsius(&k_typical, ts, vref);

            if (deci == TEMPERATURE_INVALID) {
                if (seen_valid) {
                    run_ended = 1U;
                }
                continue;
            }
            /* Valid again after the run ended means a discontinuity. */
            if (run_ended) {
                breaks++;
                break;
            }
            if (seen_valid && (deci < previous)) {
                breaks++;
                break;
            }
            previous   = deci;
            seen_valid = 1U;
        }
    }
    CHECK(breaks == 0U);
}

/* ------------------------------------------------------------------ */

int main(void) {
    (void)printf("unit: ADC counts to degrees\n");

    calibration_points_are_exact();
    midpoint_interpolates();
    vdda_correction_recovers_the_real_temperature();
    skipping_the_correction_would_be_wildly_wrong();
    hotter_counts_read_hotter();
    below_zero_is_representable();
    blank_flash_is_rejected();
    degenerate_calibration_is_rejected();
    degenerate_readings_are_rejected();
    implausible_results_are_rejected();
    whole_input_domain_is_monotonic();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
