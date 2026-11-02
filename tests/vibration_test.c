/**
 * @file vibration_test.c
 * @brief The vibration figure: that gravity really is gone, that a known
 *        waveform gives its exact RMS, and that nothing overflows at the
 *        sensor's limits.
 *
 * Every expected value here is worked out from the definition of root mean
 * square and written as a number, not borrowed from the module. A test that
 * computes the expectation the same way the code does would pass whatever the
 * code did.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "vibration.h"

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

/// Report the value too, because "not 692" says nothing about which way.
static void check_milli_g(uint32_t got, uint32_t want, int line) {
    g_checks++;
    if (got != want) {
        g_failures++;
        (void)printf("  FAIL line %d: got %u mg, wanted %u mg\n", line,
                     (unsigned)got, (unsigned)want);
    }
}

#define CHECK_MG(got, want) check_milli_g((got), (want), __LINE__)

/* The window length, spelled out again rather than taken from the header: a
   test that reads the same constant proves only that it equals itself. */
#define EXPECT_WINDOW 160U

/// An ADXL345 in full resolution, at any range. 4 mg to the count.
#define SENSOR_SCALE 4U

/// One count short of the full-resolution range, used for the overflow case.
#define NEAR_FULL_SCALE 1023

/// Pick between two samples without the int promotion a ternary forces, which
/// then has to narrow back and is implementation defined on the way.
static int16_t pick(uint32_t index, int16_t even, int16_t odd) {
    if ((index % 2U) == 0U) {
        return even;
    }
    return odd;
}

/// Push a whole window of one repeated sample, and return what came out.
/// Fails loudly if the window did not complete exactly on the last sample.
static uint32_t push_constant(int16_t x, int16_t y, int16_t z) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    vibration_init(&window, SENSOR_SCALE);

    for (uint32_t i = 0U; i < EXPECT_WINDOW - 1U; i++) {
        CHECK(vibration_push(&window, x, y, z, &result) == 0U);
    }
    CHECK(vibration_push(&window, x, y, z, &result) == 1U);
    return result;
}

/// Push a whole window alternating between two samples on one axis.
static uint32_t push_alternating(int16_t high, int16_t low) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    vibration_init(&window, SENSOR_SCALE);

    for (uint32_t i = 0U; i < EXPECT_WINDOW; i++) {
        const int16_t value = pick(i, high, low);
        (void)vibration_push(&window, value, 0, 0, &result);
    }
    return result;
}

/* --- the tests --------------------------------------------------------- */

/// A sensor lying still reads gravity forever. If that came out as vibration
/// every reading would be about 1000 mg and the fan would be invisible.
static void a_still_sensor_reads_no_vibration(void) {
    CHECK_MG(push_constant(0, 0, 0), 0U);
    CHECK_MG(push_constant(256, 0, 0), 0U);       /* 1 g on X */
    CHECK_MG(push_constant(0, 0, 256), 0U);       /* 1 g on Z, flat on a desk */
    CHECK_MG(push_constant(148, 148, 148), 0U);   /* glued on at some angle */
}

/// The same, downwards. This is the case a mean computed by integer division
/// gets wrong: truncation runs towards zero, so a negative steady value
/// leaves a residue behind and invents vibration that is not there.
static void gravity_the_other_way_round_also_vanishes(void) {
    CHECK_MG(push_constant(-256, 0, 0), 0U);
    CHECK_MG(push_constant(0, -256, 0), 0U);
    CHECK_MG(push_constant(-1024, -1024, -1024), 0U);
    CHECK_MG(push_constant(-1, -1, -1), 0U);
}

/// A square wave of plus and minus A has an RMS of exactly A, so the answer
/// must be A counts times the scale, to the milli-g.
static void a_square_wave_gives_its_own_amplitude(void) {
    CHECK_MG(push_alternating(1, -1), 1U * SENSOR_SCALE);
    CHECK_MG(push_alternating(10, -10), 10U * SENSOR_SCALE);
    CHECK_MG(push_alternating(100, -100), 100U * SENSOR_SCALE);
}

/// And it must not care where the wave sits. Riding on 1 g gives the same
/// answer as riding on nothing, which is the whole point of the subtraction.
static void a_square_wave_on_top_of_gravity_reads_the_same(void) {
    CHECK_MG(push_alternating(256 + 20, 256 - 20), 20U * SENSOR_SCALE);
    CHECK_MG(push_alternating(-256 + 20, -256 - 20), 20U * SENSOR_SCALE);
}

/// Push a whole window alternating on all three axes at once.
static uint32_t push_alternating_xyz(int16_t high, int16_t low) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    vibration_init(&window, SENSOR_SCALE);

    for (uint32_t i = 0U; i < EXPECT_WINDOW; i++) {
        const int16_t v = pick(i, high, low);
        (void)vibration_push(&window, v, v, v, &result);
    }
    return result;
}

/// Shaking equally on three perpendicular axes is not three times one axis,
/// it is the square root of three times one axis, because that is how
/// variance adds across directions. 100 counts a side is 400 mg a side, and
/// 400 times 1.7320508 is 692.8, which floors to 692.
static void three_axes_add_as_a_right_angled_triangle(void) {
    CHECK_MG(push_alternating_xyz(100, -100), 692U);
    CHECK_MG(push_alternating_xyz(10, -10), 69U);  /* 40 * 1.7320508 = 69.28 */
}

/// The result must arrive on the LAST sample of the window and not before,
/// or the telemetry rate drifts away from five a second.
static void the_result_arrives_exactly_on_the_last_sample(void) {
    vibration_t window;
    uint32_t    result   = 0U;
    unsigned    produced = 0U;
    vibration_init(&window, SENSOR_SCALE);

    for (uint32_t i = 0U; i < 3U * EXPECT_WINDOW; i++) {
        if (vibration_push(&window, 50, 0, 0, &result) == 1U) {
            produced++;
            CHECK(((i + 1U) % EXPECT_WINDOW) == 0U);
        }
    }
    CHECK(produced == 3U);
}

/// Each window must stand on its own. If the sums carried over, a quiet fan
/// after a shake would keep reporting the shake.
static void a_window_does_not_remember_the_one_before(void) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    vibration_init(&window, SENSOR_SCALE);

    /* First window: shaking hard. */
    for (uint32_t i = 0U; i < EXPECT_WINDOW; i++) {
        const int16_t v = pick(i, 500, -500);
        (void)vibration_push(&window, v, 0, 0, &result);
    }
    CHECK_MG(result, 500U * SENSOR_SCALE);

    /* Second window: perfectly still, on the same accumulator. */
    for (uint32_t i = 0U; i < EXPECT_WINDOW; i++) {
        (void)vibration_push(&window, 256, 0, 0, &result);
    }
    CHECK_MG(result, 0U);
}

/// One sample out of place in an otherwise still window. Worked out by hand:
/// the mean is 1 count, so the deviations are 159 of minus one and one of
/// 159, the mean square is 159, and the root of that is 12.6 counts, which
/// is 50.4 mg.
static void a_single_knock_is_not_lost_or_exaggerated(void) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    vibration_init(&window, SENSOR_SCALE);

    (void)vibration_push(&window, 160, 0, 0, &result);
    for (uint32_t i = 1U; i < EXPECT_WINDOW; i++) {
        (void)vibration_push(&window, 0, 0, 0, &result);
    }
    CHECK_MG(result, 50U);
}

/// The sensor's limit, on all three axes, which is the largest number the
/// arithmetic can ever be handed. 1023 counts a side is 1771.9 counts
/// combined, which is 7087.5 mg. Anything that wrapped would come out small.
static void the_sensors_full_range_does_not_overflow(void) {
    const uint32_t result = push_alternating_xyz(NEAR_FULL_SCALE,
                                                 -NEAR_FULL_SCALE);
    CHECK_MG(result, 7087U);
    CHECK(result > 7000U);  /* a wrap would land nowhere near here */
}

/// The scale is the sensor's, not the module's. Halving it must halve the
/// answer, so a differently configured part needs no change in here.
static void the_injected_scale_is_what_converts(void) {
    vibration_t window;
    uint32_t    result = 0xFFFFFFFFU;
    const uint32_t one_mg_per_count = 1U;

    vibration_init(&window, one_mg_per_count);
    for (uint32_t i = 0U; i < EXPECT_WINDOW; i++) {
        const int16_t v = pick(i, 100, -100);
        (void)vibration_push(&window, v, 0, 0, &result);
    }
    CHECK_MG(result, 100U);  /* 100 counts, one mg each */
}

int main(void) {
    (void)printf("unit: vibration as the RMS of the alternating part\n");

    a_still_sensor_reads_no_vibration();
    gravity_the_other_way_round_also_vanishes();
    a_square_wave_gives_its_own_amplitude();
    a_square_wave_on_top_of_gravity_reads_the_same();
    three_axes_add_as_a_right_angled_triangle();
    the_result_arrives_exactly_on_the_last_sample();
    a_window_does_not_remember_the_one_before();
    a_single_knock_is_not_lost_or_exaggerated();
    the_sensors_full_range_does_not_overflow();
    the_injected_scale_is_what_converts();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
