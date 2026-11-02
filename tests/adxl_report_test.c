/**
 * @file adxl_report_test.c
 * @brief The repeated accelerometer status: when it speaks, what it says, and
 *        how many times it touches the sensor.
 *
 * The probe is a fake that counts its calls and answers whatever the test
 * tells it to, so "brought up once, never again" is checked by counting
 * rather than by reading the code. The sink captures the last line and the
 * total, because a status that speaks twice as often as it should is as wrong
 * as one that never speaks.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "adxl_report.h"

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

/* The lines are spelled out again here rather than taken from the header. A
   test that compares a constant against itself proves only that the constant
   equals itself, and the host splits these strings at the first comma. */
static const char k_expect_ready[]  = "adxl,ready\n";
static const char k_expect_wrong[]  = "adxl,wrong\n";
static const char k_expect_absent[] = "adxl,absent\n";

/// The period the module promises, repeated here for the same reason.
#define EXPECT_PERIOD_MS 2000U

/* --- the fake probe ---------------------------------------------------- */

static adxl_presence_t g_probe_answer;
static unsigned        g_probe_calls;

static adxl_presence_t fake_probe(void) {
    g_probe_calls++;
    return g_probe_answer;
}

/* --- the capturing sink ------------------------------------------------ */

#define LINE_MAX 64U
static char     g_last_line[LINE_MAX];
static unsigned g_lines_sent;

static void fake_sink(const char *line) {
    g_lines_sent++;
    (void)strncpy(g_last_line, line, LINE_MAX - 1U);
    g_last_line[LINE_MAX - 1U] = '\0';
}

/// One fresh module plus fresh fakes, so no test inherits another's counters.
static void start(adxl_report_state_t *state, adxl_presence_t answer) {
    adxl_report_init(state);
    g_probe_answer = answer;
    g_probe_calls  = 0U;
    g_lines_sent   = 0U;
    g_last_line[0] = '\0';
}

/* --- the tests --------------------------------------------------------- */

/// Nothing is sent at start-up, so the very first call must speak. The old
/// one-shot line was missed exactly because a person cannot aim at a moment.
static void the_first_call_speaks_at_once(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_READY);

    CHECK(adxl_report_update(&state, 0U, fake_probe, fake_sink) == 1U);
    CHECK(g_lines_sent == 1U);
    CHECK(strcmp(g_last_line, k_expect_ready) == 0);
}

/// A loop running at megahertz calls this constantly. Only the period may
/// let a line out, or the wire would carry nothing else.
static void nothing_comes_out_before_the_period(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_READY);

    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    CHECK(g_lines_sent == 1U);

    for (uint32_t t = 1U; t < EXPECT_PERIOD_MS; t++) {
        CHECK(adxl_report_update(&state, t, fake_probe, fake_sink) == 0U);
    }
    CHECK(g_lines_sent == 1U);

    CHECK(adxl_report_update(&state, EXPECT_PERIOD_MS, fake_probe,
                             fake_sink) == 1U);
    CHECK(g_lines_sent == 2U);
}

/// The clock is a 32 bit millisecond counter, so it wraps after 49 days. The
/// comparison is a subtraction for this reason, and this is what checks it.
static void a_clock_about_to_wrap_keeps_speaking(void) {
    adxl_report_state_t state;
    const uint32_t before_the_end = 0xFFFFFFFFUL - 100U;
    start(&state, ADXL_PRESENCE_ABSENT);

    (void)adxl_report_update(&state, before_the_end, fake_probe, fake_sink);
    CHECK(g_lines_sent == 1U);

    /* 100 ms to the end of the counter, then past it: still short of the
       period, so still quiet. */
    CHECK(adxl_report_update(&state, 0U, fake_probe, fake_sink) == 0U);
    CHECK(adxl_report_update(&state, 500U, fake_probe, fake_sink) == 0U);

    /* Now the period really has passed, across the wrap. */
    CHECK(adxl_report_update(&state, EXPECT_PERIOD_MS - 100U, fake_probe,
                             fake_sink) == 1U);
    CHECK(g_lines_sent == 2U);
}

/// A sensor that is not there gets probed again on every period. That is what
/// lets Bogdan fix the wiring on a running board and see it start working,
/// instead of reflashing to find out.
static void an_absent_sensor_is_tried_again(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_ABSENT);

    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    (void)adxl_report_update(&state, EXPECT_PERIOD_MS, fake_probe, fake_sink);
    (void)adxl_report_update(&state, 2U * EXPECT_PERIOD_MS, fake_probe,
                             fake_sink);

    CHECK(g_probe_calls == 3U);
    CHECK(g_lines_sent == 3U);
    CHECK(strcmp(g_last_line, k_expect_absent) == 0);
}

/// The same for a wrong device: grounding SDO changes the address, so the
/// next try can succeed without a reset.
static void a_wrong_device_is_tried_again(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_WRONG);

    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    CHECK(strcmp(g_last_line, k_expect_wrong) == 0);

    (void)adxl_report_update(&state, EXPECT_PERIOD_MS, fake_probe, fake_sink);
    CHECK(g_probe_calls == 2U);
    CHECK(strcmp(g_last_line, k_expect_wrong) == 0);
}

/// Once it is up it is never configured again. Rewriting the configuration
/// registers restarts the sensor's FIFO, which would throw away samples
/// under whoever is reading them.
static void a_working_sensor_is_configured_once(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_READY);

    for (uint32_t period = 0U; period < 10U; period++) {
        (void)adxl_report_update(&state, period * EXPECT_PERIOD_MS,
                                 fake_probe, fake_sink);
    }

    CHECK(g_probe_calls == 1U);
    CHECK(g_lines_sent == 10U);
    CHECK(strcmp(g_last_line, k_expect_ready) == 0);
}

/// The retry has to actually recover, not just keep trying: an absent sensor
/// that starts answering must be brought up and reported ready.
static void a_sensor_that_appears_later_is_brought_up(void) {
    adxl_report_state_t state;
    start(&state, ADXL_PRESENCE_ABSENT);

    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    CHECK(strcmp(g_last_line, k_expect_absent) == 0);

    g_probe_answer = ADXL_PRESENCE_READY;  /* the wire goes in */
    (void)adxl_report_update(&state, EXPECT_PERIOD_MS, fake_probe, fake_sink);
    CHECK(strcmp(g_last_line, k_expect_ready) == 0);
    CHECK(g_probe_calls == 2U);

    /* And from here it is up, so it stops being configured. */
    (void)adxl_report_update(&state, 2U * EXPECT_PERIOD_MS, fake_probe,
                             fake_sink);
    CHECK(g_probe_calls == 2U);
}

/// Nothing may claim the sensor works unless the probe said so. That is the
/// direction that costs somebody an hour: `ready` with no sensor sends them
/// looking for a fault that is not there, while `absent` with a working
/// sensor is corrected by the next period.
///
/// The fresh state starts at absent precisely so an answer that never arrived
/// cannot read as a working sensor.
static void nothing_claims_ready_unless_the_probe_said_so(void) {
    adxl_report_state_t state;
    adxl_report_init(&state);
    CHECK(state.presence == ADXL_PRESENCE_ABSENT);

    start(&state, ADXL_PRESENCE_WRONG);
    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    CHECK(strcmp(g_last_line, k_expect_ready) != 0);

    start(&state, ADXL_PRESENCE_ABSENT);
    (void)adxl_report_update(&state, 0U, fake_probe, fake_sink);
    CHECK(strcmp(g_last_line, k_expect_ready) != 0);
    CHECK(strcmp(g_last_line, k_expect_absent) == 0);
}

int main(void) {
    (void)printf("unit: the repeated accelerometer status\n");

    the_first_call_speaks_at_once();
    nothing_comes_out_before_the_period();
    a_clock_about_to_wrap_keeps_speaking();
    an_absent_sensor_is_tried_again();
    a_wrong_device_is_tried_again();
    a_working_sensor_is_configured_once();
    a_sensor_that_appears_later_is_brought_up();
    nothing_claims_ready_unless_the_probe_said_so();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
