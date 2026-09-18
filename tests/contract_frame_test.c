/**
 * @file contract_frame_test.c
 * @brief Contract test: pins the telemetry frames to the host's parser rules.
 *
 * This is the device half of the `firmware -> industrial-hmi` contract
 * (ADR-0029 in industrial-hmi): the serial wire protocol `sensorId,value\n`.
 * Nothing here talks to a board. `telemetry` is pure logic driven by injected
 * function pointers, so a fake temperature reader and a capturing sink are
 * enough to see every byte this firmware would put on the wire.
 *
 * It works in two layers, and the two-layer shape is the whole point:
 *
 *   1. frame_gate_selfcheck() drives the gate with samples copied verbatim
 *      from the host's SerialFrameParserTest. If the transcribed rules ever
 *      stop matching the host's, the test fails HERE, before it says anything
 *      about the firmware.
 *
 *   2. The firmware cases push telemetry's real output through that gate, and
 *      also assert the exact bytes.
 *
 * Layered this way, the expectations cannot simply be edited to match a
 * changed firmware: the gate is anchored to the host's own samples. Assert
 * only the literal strings and a format change could be "fixed" on both sides
 * at once, leaving the contract silently broken.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "frame_gate.h"
#include "telemetry.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Minimal assertion harness. No test framework: nothing to install,   */
/* and the firmware toolchain stays untouched.                         */
/* ------------------------------------------------------------------ */

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

/// Record one string expectation, showing both sides when they differ.
static void check_str(const char *actual, const char *expected, int line) {
    g_checks++;
    if ((actual == NULL) || (strcmp(actual, expected) != 0)) {
        g_failures++;
        (void)printf("  FAIL line %d: expected \"%s\", got \"%s\"\n", line,
                     expected, (actual != NULL) ? actual : "(null)");
    }
}

#define CHECK(cond)               check((cond), #cond, __LINE__)
#define CHECK_STR(actual, expect) check_str((actual), (expect), __LINE__)

/* ------------------------------------------------------------------ */
/* Test doubles for the two function pointers telemetry depends on.    */
/* ------------------------------------------------------------------ */

#define CAPTURE_MAX_LINES  16U
#define CAPTURE_MAX_LINE   128U
#define CAPTURE_MAX_STREAM 1024U

static char     g_lines[CAPTURE_MAX_LINES][CAPTURE_MAX_LINE];
static size_t   g_line_count;
static char     g_stream[CAPTURE_MAX_STREAM];
static size_t   g_stream_len;
static unsigned g_capture_overflow;

/// Forget everything captured so far, before a fresh scenario.
static void capture_reset(void) {
    g_line_count       = 0U;
    g_stream_len       = 0U;
    g_stream[0]        = '\0';
    g_capture_overflow = 0U;
}

/// Stand-in for uart_send_string: keeps each line AND the concatenated stream.
static void capture_sink(const char *line) {
    const size_t len = strlen(line);

    if ((g_line_count >= CAPTURE_MAX_LINES) || (len >= CAPTURE_MAX_LINE) ||
        ((g_stream_len + len) >= CAPTURE_MAX_STREAM)) {
        g_capture_overflow = 1U;
        return;
    }
    (void)memcpy(g_lines[g_line_count], line, len + 1U);
    g_line_count++;

    (void)memcpy(&g_stream[g_stream_len], line, len + 1U);
    g_stream_len += len;
}

static uint32_t g_fake_raw;

/// Stand-in for adc_temp_read: returns whatever a scenario dialled in.
static uint32_t fake_temp_reader(void) {
    return g_fake_raw;
}

/// One press: down edge then release, with the reader returning `raw`.
static void press_button(telemetry_state_t *state, uint32_t raw) {
    g_fake_raw = raw;
    (void)telemetry_update(state, 1U, fake_temp_reader, capture_sink);
    (void)telemetry_update(state, 0U, fake_temp_reader, capture_sink);
}

/* ------------------------------------------------------------------ */
/* Layer 1: the gate itself, against the host's own samples.           */
/* ------------------------------------------------------------------ */

/**
 * Every sample below is copied from SerialFrameParserTest.cpp in
 * industrial-hmi. They are the canonical statement of the wire format. If
 * this function fails, the transcribed rules have drifted from the host's and
 * nothing further in this file can be trusted.
 */
static void frame_gate_selfcheck(void) {
    frame_gate_result_t r;

    /* EmptyInputYieldsNoReadings */
    frame_gate_consume("", &r);
    CHECK(r.count == 0U);

    /* SingleCompleteFrameParses */
    frame_gate_consume("temp,23.5\n", &r);
    CHECK(r.count == 1U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK_STR(r.items[0].value, "23.5");

    /* MultipleFramesInOneChunk */
    frame_gate_consume("temp,23\nhumidity,60\n", &r);
    CHECK(r.count == 2U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK_STR(r.items[0].value, "23");
    CHECK_STR(r.items[1].sensor_id, "humidity");
    CHECK_STR(r.items[1].value, "60");

    /* EmptyLinesAreSkipped */
    frame_gate_consume("\n\ntemp,23\n\n", &r);
    CHECK(r.count == 1U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK(r.rejected == 0U);

    /* LineWithoutFieldDelimiterIsSkipped */
    frame_gate_consume("noseparator\n", &r);
    CHECK(r.count == 0U);
    CHECK(r.rejected == 1U);

    /* LineWithExtraFieldDelimiterIsSkipped */
    frame_gate_consume("a,b,c\n", &r);
    CHECK(r.count == 0U);
    CHECK(r.rejected == 1U);

    /* EmptyFieldsAreSkipped */
    frame_gate_consume(",23\n", &r);
    CHECK(r.count == 0U);
    frame_gate_consume("temp,\n", &r);
    CHECK(r.count == 0U);

    /* CrlfLineEndingIsTolerated */
    frame_gate_consume("temp,23.5\r\n", &r);
    CHECK(r.count == 1U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK_STR(r.items[0].value, "23.5");

    /* MalformedLineDoesNotDropTheValidOne */
    frame_gate_consume("garbage\ntemp,23\n", &r);
    CHECK(r.count == 1U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK_STR(r.items[0].value, "23");
    CHECK(r.rejected == 1U);

    /* PartialAfterCompleteFrameIsHeld -- the complete frame is emitted, the
       fragment is not. The host buffers it; the gate only flags it. */
    frame_gate_consume("temp,23.5\nhum", &r);
    CHECK(r.count == 1U);
    CHECK_STR(r.items[0].sensor_id, "temp");
    CHECK(r.trailing == 1U);
}

/* ------------------------------------------------------------------ */
/* Layer 2: the firmware's real output, through that gate.             */
/* ------------------------------------------------------------------ */

/// A press edge emits exactly the two contract frames, in order.
static void press_emits_state_then_temperature(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    press_button(&state, 2047U);

    CHECK(g_capture_overflow == 0U);
    CHECK(g_line_count == 2U);
    CHECK_STR(g_lines[0], "equipment/0/state,on\n");
    CHECK_STR(g_lines[1], "temp,2047\n");
}

/// The equipment state toggles, so the second press must report "off".
static void second_press_toggles_state_off(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    press_button(&state, 2047U);
    press_button(&state, 2048U);

    CHECK(g_line_count == 4U);
    CHECK_STR(g_lines[2], "equipment/0/state,off\n");
    CHECK_STR(g_lines[3], "temp,2048\n");
}

/// Only the edge counts: holding the button must not flood the wire.
static void held_button_emits_nothing_further(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    g_fake_raw = 100U;
    CHECK(telemetry_update(&state, 1U, fake_temp_reader, capture_sink) == 1U);
    const size_t after_edge = g_line_count;

    for (unsigned i = 0U; i < 50U; i++) {
        CHECK(telemetry_update(&state, 1U, fake_temp_reader, capture_sink) == 0U);
    }
    CHECK(g_line_count == after_edge);
}

/// Releasing the button is not an event either.
static void release_emits_nothing(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    press_button(&state, 100U);
    const size_t after_press = g_line_count;

    CHECK(telemetry_update(&state, 0U, fake_temp_reader, capture_sink) == 0U);
    CHECK(g_line_count == after_press);
}

/// Hand-rolled decimal formatting is where a frame usually breaks. The ADC is
/// 12-bit, but the reader's type is uint32_t, so pin the type's range too.
static void raw_value_boundaries_format_correctly(void) {
    static const struct {
        uint32_t    raw;
        const char *expected;
    } cases[] = {
        { 0U,          "temp,0\n" },
        { 1U,          "temp,1\n" },
        { 9U,          "temp,9\n" },
        { 10U,         "temp,10\n" },
        { 4095U,       "temp,4095\n" },   /* 12-bit ADC full scale */
        { 1000000000U, "temp,1000000000\n" },
        { 4294967295U, "temp,4294967295\n" },
    };

    for (size_t i = 0U; i < (sizeof(cases) / sizeof(cases[0])); i++) {
        telemetry_state_t state;
        telemetry_init(&state);
        capture_reset();

        press_button(&state, cases[i].raw);

        CHECK(g_capture_overflow == 0U);
        CHECK(g_line_count == 2U);
        CHECK_STR(g_lines[1], cases[i].expected);

        /* And the extreme values must still be acceptable frames. */
        frame_gate_result_t r;
        frame_gate_consume(g_stream, &r);
        CHECK(r.count == 2U);
        CHECK(r.rejected == 0U);
    }
}

/// The headline case: the exact bytes of one press, read the way the host
/// reads them. Two readings, nothing rejected, nothing left dangling.
static void press_stream_parses_as_two_readings(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    press_button(&state, 2047U);

    frame_gate_result_t r;
    frame_gate_consume(g_stream, &r);

    CHECK(r.count == 2U);
    CHECK(r.rejected == 0U);
    CHECK(r.overflow == 0U);
    CHECK(r.trailing == 0U);  /* every frame is newline-terminated */

    CHECK_STR(r.items[0].sensor_id, "equipment/0/state");
    CHECK_STR(r.items[0].value, "on");
    CHECK_STR(r.items[1].sensor_id, "temp");
    CHECK_STR(r.items[1].value, "2047");
}

/// A whole session of presses stays parseable end to end, with the sensor ids
/// stable: the host keys its dashboard off them.
static void session_stream_stays_parseable(void) {
    telemetry_state_t state;
    telemetry_init(&state);
    capture_reset();

    press_button(&state, 0U);
    press_button(&state, 4095U);
    press_button(&state, 42U);

    frame_gate_result_t r;
    frame_gate_consume(g_stream, &r);

    CHECK(r.count == 6U);
    CHECK(r.rejected == 0U);
    CHECK(r.trailing == 0U);

    for (size_t i = 0U; i < r.count; i += 2U) {
        CHECK_STR(r.items[i].sensor_id, "equipment/0/state");
        CHECK_STR(r.items[i + 1U].sensor_id, "temp");
    }
    CHECK_STR(r.items[0].value, "on");
    CHECK_STR(r.items[2].value, "off");
    CHECK_STR(r.items[4].value, "on");
}

/* ------------------------------------------------------------------ */

int main(void) {
    (void)printf("contract: firmware frames vs industrial-hmi parser\n");

    frame_gate_selfcheck();
    press_emits_state_then_temperature();
    second_press_toggles_state_off();
    held_button_emits_nothing_further();
    release_emits_nothing();
    raw_value_boundaries_format_correctly();
    press_stream_parses_as_two_readings();
    session_stream_stays_parseable();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
