/**
 * @file contract_accel_test.c
 * @brief The accelerometer's two new frames, checked against the HOST's
 *        acceptance rules rather than against this repo's own opinion.
 *
 * The real modules run here, not stand-ins: `adxl_report` and `vib_telemetry`
 * write through a capturing sink, and every byte they produce goes through
 * `frame_gate`, which is a transcription of industrial-hmi's
 * SerialFrameParser rules (see frame_gate.h).
 *
 * What this pins: the firmware cannot start emitting a shape the host would
 * throw away without this test going red. Adding a second comma, losing the
 * newline or emitting an empty value are all silent on the board and loud
 * here.
 *
 * What it does NOT pin, and the distinction matters: the host PARSES these
 * lines, which is not the same as doing anything with them. See the board
 * entry to industrial-hmi dated 2026-11-02.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "adxl_report.h"
#include "frame_gate.h"
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

/* --- the wire, captured ------------------------------------------------- */

#define CAPTURE_MAX 1024U
static char   g_stream[CAPTURE_MAX];
static size_t g_stream_len;

static void capture_reset(void) {
    g_stream_len = 0U;
    g_stream[0]  = '\0';
}

static void capture_sink(const char *line) {
    const size_t len = strlen(line);
    if ((g_stream_len + len) >= CAPTURE_MAX) {
        return;
    }
    (void)memcpy(&g_stream[g_stream_len], line, len + 1U);
    g_stream_len += len;
}

/* --- fakes for the two modules ------------------------------------------ */

static adxl_presence_t g_presence;
static adxl_presence_t fake_probe(void) { return g_presence; }

static uint8_t g_waiting;
static int16_t g_next_x;

static int fake_level(void *ctx, uint8_t *out_entries) {
    (void)ctx;
    *out_entries = g_waiting;
    return 0;
}

static int fake_read(void *ctx, int16_t *x, int16_t *y, int16_t *z) {
    (void)ctx;
    *x = g_next_x;
    *y = 0;
    *z = 0;
    g_next_x = (int16_t)(-g_next_x);
    return 0;
}

static const vib_sensor_t k_sensor = {NULL, fake_level, fake_read};

/* The sensor's scale, spelled out rather than taken from the device header:
   this file is about the shape on the wire, not about the part. */
#define SENSOR_SCALE 4U
#define PERIOD_MS    20U
#define WINDOW       160U

/* --- the tests ---------------------------------------------------------- */

/// Emit one status line of each kind and run the whole stream through the
/// host's rules. Three readings, nothing rejected, nothing left dangling.
static void every_status_line_is_a_reading_the_host_accepts(void) {
    const adxl_presence_t kinds[] = {ADXL_PRESENCE_ABSENT, ADXL_PRESENCE_WRONG,
                                     ADXL_PRESENCE_READY};
    const char *expect[] = {"absent", "wrong", "ready"};
    frame_gate_result_t parsed;
    capture_reset();

    for (unsigned i = 0U; i < 3U; i++) {
        adxl_report_state_t report;
        adxl_report_init(&report);
        g_presence = kinds[i];
        (void)adxl_report_update(&report, 0U, fake_probe, capture_sink);
    }

    frame_gate_consume(g_stream, &parsed);
    CHECK(parsed.count == 3U);
    CHECK(parsed.rejected == 0U);
    CHECK(parsed.trailing == 0U);
    CHECK(parsed.overflow == 0U);
    for (unsigned i = 0U; i < 3U && i < parsed.count; i++) {
        CHECK(strcmp(parsed.items[i].sensor_id, "adxl") == 0);
        CHECK(strcmp(parsed.items[i].value, expect[i]) == 0);
    }
}

/// Fill one window and run the line through the host's rules. The value
/// stays TEXT on the host side, so what is pinned is the shape, not a number.
static void a_vibration_line_is_a_reading_the_host_accepts(void) {
    vib_telemetry_t     stream;
    frame_gate_result_t parsed;
    capture_reset();
    vib_telemetry_init(&stream, SENSOR_SCALE);
    g_waiting = 16U;
    g_next_x  = 100;

    for (uint32_t poll = 0U; poll < (WINDOW / 16U); poll++) {
        (void)vib_telemetry_poll(&stream, poll * PERIOD_MS, &k_sensor,
                                 capture_sink);
    }

    frame_gate_consume(g_stream, &parsed);
    CHECK(parsed.count == 1U);
    CHECK(parsed.rejected == 0U);
    CHECK(parsed.trailing == 0U);
    CHECK(strcmp(parsed.items[0].sensor_id, "vib") == 0);
    CHECK(strcmp(parsed.items[0].value, "400") == 0);
}

/// The real interleaving: a status line every two seconds while vibration
/// lines go out five times a second. The host must get both kinds out of one
/// stream, in order, with nothing rejected.
static void both_kinds_survive_being_mixed_on_one_wire(void) {
    adxl_report_state_t report;
    vib_telemetry_t     stream;
    frame_gate_result_t parsed;
    capture_reset();
    adxl_report_init(&report);
    vib_telemetry_init(&stream, SENSOR_SCALE);
    g_presence = ADXL_PRESENCE_READY;
    g_next_x   = 100;

    /* Two samples a poll rather than sixteen, so a window takes 1.6 s and a
       few seconds of board time produce a handful of readings instead of
       dozens. frame_gate holds eight, and this test is about the SHAPE of
       what comes out, not about how much of it there is. */
    g_waiting = 2U;

    /* 4200 ms of board time: two vibration windows and three status lines. */
    for (uint32_t ms = 0U; ms <= 4200U; ms += PERIOD_MS) {
        (void)adxl_report_update(&report, ms, fake_probe, capture_sink);
        (void)vib_telemetry_poll(&stream, ms, &k_sensor, capture_sink);
    }

    frame_gate_consume(g_stream, &parsed);
    CHECK(parsed.rejected == 0U);
    CHECK(parsed.trailing == 0U);
    CHECK(parsed.overflow == 0U);
    CHECK(parsed.count >= 3U);

    unsigned statuses = 0U;
    unsigned vibs     = 0U;
    for (size_t i = 0U; i < parsed.count; i++) {
        if (strcmp(parsed.items[i].sensor_id, "adxl") == 0) {
            statuses++;
        } else if (strcmp(parsed.items[i].sensor_id, "vib") == 0) {
            vibs++;
        } else {
            CHECK(0);  /* a third id would be a frame nobody agreed to */
        }
    }
    CHECK(statuses > 0U);
    CHECK(vibs > 0U);
    CHECK((statuses + vibs) == parsed.count);
}

/// The ids carry no comma, which is the rule the host splits on. If either
/// ever gained one, every line would hold two and the host would throw the
/// whole reading away without a sound.
static void neither_sensor_id_contains_the_separator(void) {
    frame_gate_result_t parsed;
    adxl_report_state_t report;
    capture_reset();
    adxl_report_init(&report);
    g_presence = ADXL_PRESENCE_READY;
    (void)adxl_report_update(&report, 0U, fake_probe, capture_sink);

    frame_gate_consume(g_stream, &parsed);
    CHECK(parsed.count == 1U);
    CHECK(strchr(parsed.items[0].sensor_id, ',') == NULL);
    CHECK(strchr(parsed.items[0].value, ',') == NULL);
}

/// A silent sensor must put NOTHING on the wire, not a reading the host would
/// happily accept and then show as a measurement nobody made.
static void a_silent_sensor_contributes_no_reading(void) {
    vib_telemetry_t     stream;
    frame_gate_result_t parsed;
    capture_reset();
    vib_telemetry_init(&stream, SENSOR_SCALE);
    g_waiting = 0U;

    for (uint32_t poll = 0U; poll < 200U; poll++) {
        (void)vib_telemetry_poll(&stream, poll * PERIOD_MS, &k_sensor,
                                 capture_sink);
    }

    frame_gate_consume(g_stream, &parsed);
    CHECK(g_stream_len == 0U);
    CHECK(parsed.count == 0U);
    CHECK(parsed.rejected == 0U);
}

int main(void) {
    (void)printf("unit: the accelerometer frames against the host's rules\n");

    every_status_line_is_a_reading_the_host_accepts();
    a_vibration_line_is_a_reading_the_host_accepts();
    both_kinds_survive_being_mixed_on_one_wire();
    neither_sensor_id_contains_the_separator();
    a_silent_sensor_contributes_no_reading();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
