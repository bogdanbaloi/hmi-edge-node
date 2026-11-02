/**
 * @file vib_telemetry_test.c
 * @brief When the stream speaks, what it says, and every case where it must
 *        say nothing at all rather than something plausible.
 *
 * The sensor is a fake queue that counts how many samples were taken, so
 * "it never takes more than were waiting" is a number here rather than a
 * reading of the code. The sink captures every line, because a stream that
 * speaks twice per window is as wrong as one that never speaks.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "vib_telemetry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned g_checks;
static unsigned g_failures;

static void check(int condition, const char *what, int line) {
    g_checks++;
    if (!condition) {
        g_failures++;
        (void)printf("  FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static void check_text(const char *got, const char *want, int line) {
    g_checks++;
    if (strcmp(got, want) != 0) {
        g_failures++;
        (void)printf("  FAIL line %d: got \"%s\", wanted \"%s\"\n", line, got,
                     want);
    }
}

#define CHECK_TEXT(got, want) check_text((got), (want), __LINE__)

/* Spelled out again rather than taken from the headers: a test that reads the
   same constant proves only that the constant equals itself. */
#define EXPECT_PERIOD_MS 20U
#define EXPECT_WINDOW    160U
#define EXPECT_FULL      32U
#define SENSOR_SCALE     4U

/* --- the fake sensor ---------------------------------------------------- */

typedef struct {
    uint8_t  waiting;       ///< what fifo_level will report
    unsigned level_calls;
    unsigned samples_taken;
    int      fail_level;    ///< non zero makes fifo_level fail
    int      fail_read_at;  ///< fail the read with this index, or -1
    int16_t  next_x;        ///< handed out, then negated, so it alternates
} fake_sensor_t;

static fake_sensor_t g_fake;

/// A failing read is allowed to have written part of its answer already, so
/// the fake leaves rubbish behind on failure. A fake that politely writes
/// nothing would hide a caller that forgets to check the return value.
#define RUBBISH_LEFT_BY_A_FAILED_READ 7U

static int fake_level(void *ctx, uint8_t *out_entries) {
    (void)ctx;
    g_fake.level_calls++;
    if (g_fake.fail_level != 0) {
        *out_entries = (uint8_t)RUBBISH_LEFT_BY_A_FAILED_READ;
        return -1;
    }
    *out_entries = g_fake.waiting;
    return 0;
}

static int fake_read(void *ctx, int16_t *x, int16_t *y, int16_t *z) {
    (void)ctx;
    if (g_fake.fail_read_at >= 0 &&
        (int)g_fake.samples_taken == g_fake.fail_read_at) {
        g_fake.samples_taken++;
        return -1;
    }
    g_fake.samples_taken++;
    *x = g_fake.next_x;
    *y = 0;
    *z = 0;
    g_fake.next_x = (int16_t)(-g_fake.next_x);
    return 0;
}

static const vib_sensor_t k_sensor = {NULL, fake_level, fake_read};

/* --- the capturing sink ------------------------------------------------- */

#define LINE_MAX 32U
static char     g_last_line[LINE_MAX];
static unsigned g_lines_sent;

static void fake_sink(const char *line) {
    g_lines_sent++;
    (void)strncpy(g_last_line, line, LINE_MAX - 1U);
    g_last_line[LINE_MAX - 1U] = '\0';
}

/// One fresh stream plus fresh fakes, so no test inherits another's counters.
static void start(vib_telemetry_t *stream, uint8_t waiting, int16_t amplitude) {
    vib_telemetry_init(stream, SENSOR_SCALE);
    g_fake.waiting       = waiting;
    g_fake.level_calls   = 0U;
    g_fake.samples_taken = 0U;
    g_fake.fail_level    = 0;
    g_fake.fail_read_at  = -1;
    g_fake.next_x        = amplitude;
    g_lines_sent         = 0U;
    g_last_line[0]       = '\0';
}

/* --- the tests ---------------------------------------------------------- */

/// The loop calls this twenty thousand times a second. Asking the sensor
/// anything on every one of those turns would fill the bus with the question.
static void the_sensor_is_not_asked_before_the_period(void) {
    vib_telemetry_t stream;
    start(&stream, 0U, 0);

    (void)vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink);
    CHECK(g_fake.level_calls == 1U);

    for (uint32_t t = 1U; t < EXPECT_PERIOD_MS; t++) {
        CHECK(vib_telemetry_poll(&stream, t, &k_sensor, fake_sink) == 0U);
    }
    CHECK(g_fake.level_calls == 1U);

    (void)vib_telemetry_poll(&stream, EXPECT_PERIOD_MS, &k_sensor, fake_sink);
    CHECK(g_fake.level_calls == 2U);
}

/// An empty queue costs one question and nothing else.
static void an_empty_queue_takes_no_samples(void) {
    vib_telemetry_t stream;
    start(&stream, 0U, 0);

    CHECK(vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink) == 0U);
    CHECK(g_fake.samples_taken == 0U);
    CHECK(g_lines_sent == 0U);
}

/// Never more than were waiting, or it would read a queue that is empty and
/// believe whatever the sensor happens to put on the bus.
static void it_takes_exactly_what_was_waiting(void) {
    vib_telemetry_t stream;
    start(&stream, 7U, 10);

    (void)vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink);
    CHECK(g_fake.samples_taken == 7U);
}

/// A whole window delivered sixteen at a time, which is what 20 ms at 800 Hz
/// actually produces. The waveform alternates plus and minus 100 counts, so
/// its RMS is 100 counts, which is 400 milli-g at 4 milli-g to the count.
static void a_full_window_puts_one_line_on_the_wire(void) {
    vib_telemetry_t stream;
    const uint8_t   per_poll = 16U;
    start(&stream, per_poll, 100);

    for (uint32_t poll = 0U; poll < (EXPECT_WINDOW / per_poll); poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }

    CHECK(g_fake.samples_taken == EXPECT_WINDOW);
    CHECK(g_lines_sent == 1U);
    CHECK_TEXT(g_last_line, "vib,400\n");
}

/// The shape is the same as every other frame on this wire: an id, a comma,
/// a value, a newline, nothing else. The host splits at the first comma.
static void the_line_has_the_shape_the_host_parses(void) {
    vib_telemetry_t stream;
    start(&stream, EXPECT_WINDOW, 1);

    (void)vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink);
    CHECK(g_lines_sent == 0U);  /* 160 is past full, so it is refused */

    start(&stream, 20U, 1);
    for (uint32_t poll = 0U; poll < 8U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK_TEXT(g_last_line, "vib,4\n");  /* 1 count RMS, 4 milli-g */
}

/// A queue found FULL means the sensor had nowhere to put the next sample,
/// so what is in it is not consecutive with what the window already holds.
/// Reporting an RMS over a window with a hole in it would be exactly the
/// plausible number this module refuses to send.
static void a_full_queue_throws_the_window_away(void) {
    vib_telemetry_t stream;
    start(&stream, 16U, 100);

    /* Nine polls of sixteen leaves the window one poll short of full. */
    for (uint32_t poll = 0U; poll < 9U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 0U);

    g_fake.waiting = (uint8_t)EXPECT_FULL;
    (void)vib_telemetry_poll(&stream, 9U * EXPECT_PERIOD_MS, &k_sensor,
                             fake_sink);
    CHECK(g_lines_sent == 0U);
    CHECK(g_fake.samples_taken == 9U * 16U);  /* it took none of them */

    /* A fresh window at a DIFFERENT amplitude, which is what makes this
       check bite: 10 counts is 40 milli-g. If the 144 samples from before
       had been kept, the window would complete on the FIRST poll below and
       report a figure mixed out of two amplitudes. */
    g_fake.waiting = 16U;
    g_fake.next_x  = 10;
    (void)vib_telemetry_poll(&stream, 10U * EXPECT_PERIOD_MS, &k_sensor,
                             fake_sink);
    CHECK(g_lines_sent == 0U);

    for (uint32_t poll = 11U; poll < 20U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 1U);
    CHECK_TEXT(g_last_line, "vib,40\n");
}

/// A bus that fails while answering how full it is. Nothing is taken and
/// nothing is sent, because there is no honest number to send.
static void a_failed_question_sends_nothing(void) {
    vib_telemetry_t stream;
    start(&stream, 16U, 100);

    g_fake.fail_level = 1;
    CHECK(vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink) == 0U);
    CHECK(g_fake.samples_taken == 0U);
    CHECK(g_lines_sent == 0U);
}

/// A bus that fails partway through a window. The samples already collected
/// are no longer known to be consecutive with whatever comes next, so the
/// window goes and the wire stays quiet.
static void a_failure_partway_throws_the_window_away(void) {
    vib_telemetry_t stream;
    start(&stream, 16U, 100);

    for (uint32_t poll = 0U; poll < 5U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_fake.samples_taken == 80U);

    g_fake.fail_read_at = 84;  /* five samples into the sixth poll */
    (void)vib_telemetry_poll(&stream, 5U * EXPECT_PERIOD_MS, &k_sensor,
                             fake_sink);
    CHECK(g_lines_sent == 0U);

    /* A fresh window at a DIFFERENT amplitude. 10 counts is 40 milli-g, and
       only a window that really started over can report it: keeping the 84
       samples from before would mix two amplitudes into one figure. */
    g_fake.fail_read_at = -1;
    g_fake.next_x       = 10;
    for (uint32_t poll = 6U; poll < 16U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 1U);
    CHECK_TEXT(g_last_line, "vib,40\n");
}

/// A bus that fails on the question must not leave the window standing
/// either. The fake writes rubbish into the count before failing, which is
/// what a half finished read really does, so a caller that ignores the
/// return value drains samples that were never there.
static void a_failed_question_also_throws_the_window_away(void) {
    vib_telemetry_t stream;
    start(&stream, 16U, 100);

    for (uint32_t poll = 0U; poll < 9U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_fake.samples_taken == 144U);

    g_fake.fail_level = 1;
    (void)vib_telemetry_poll(&stream, 9U * EXPECT_PERIOD_MS, &k_sensor,
                             fake_sink);
    CHECK(g_fake.samples_taken == 144U);  /* the rubbish count was not used */

    g_fake.fail_level = 0;
    g_fake.next_x     = 10;
    for (uint32_t poll = 10U; poll < 20U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 1U);
    CHECK_TEXT(g_last_line, "vib,40\n");
}

/// Nothing this module can be told makes it put a number on the wire for a
/// sensor that is not answering. Whether the sensor is there is said by
/// adxl_report, separately and repeatedly, in words.
static void a_silent_sensor_never_produces_a_number(void) {
    vib_telemetry_t stream;
    start(&stream, 0U, 0);

    for (uint32_t poll = 0U; poll < 100U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 0U);

    start(&stream, 16U, 100);
    g_fake.fail_level = 1;
    for (uint32_t poll = 0U; poll < 100U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * EXPECT_PERIOD_MS, &k_sensor,
                                 fake_sink);
    }
    CHECK(g_lines_sent == 0U);
}

/// The clock is a 32 bit millisecond counter, so it wraps after 49 days.
static void a_clock_about_to_wrap_keeps_polling(void) {
    vib_telemetry_t stream;
    const uint32_t  before_the_end = 0xFFFFFFFFUL - 5U;
    start(&stream, 0U, 0);

    (void)vib_telemetry_poll(&stream, before_the_end, &k_sensor, fake_sink);
    CHECK(g_fake.level_calls == 1U);

    /* Six milliseconds later, across the wrap, still inside the period. */
    CHECK(vib_telemetry_poll(&stream, 0U, &k_sensor, fake_sink) == 0U);
    CHECK(g_fake.level_calls == 1U);

    /* And now the period really has passed, across the wrap. */
    (void)vib_telemetry_poll(&stream, EXPECT_PERIOD_MS - 5U, &k_sensor,
                             fake_sink);
    CHECK(g_fake.level_calls == 2U);
}

int main(void) {
    (void)printf("unit: the vibration stream on the wire\n");

    the_sensor_is_not_asked_before_the_period();
    an_empty_queue_takes_no_samples();
    it_takes_exactly_what_was_waiting();
    a_full_window_puts_one_line_on_the_wire();
    the_line_has_the_shape_the_host_parses();
    a_full_queue_throws_the_window_away();
    a_failed_question_sends_nothing();
    a_failure_partway_throws_the_window_away();
    a_failed_question_also_throws_the_window_away();
    a_silent_sensor_never_produces_a_number();
    a_clock_about_to_wrap_keeps_polling();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
