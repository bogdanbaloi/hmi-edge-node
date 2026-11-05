/**
 * @file wire_probe_test.c
 * @brief Host tests for the wire probe: is a wire physically there?
 *
 * The fake pin reader answers from a table AND records every question it was
 * asked, because half of what this module gets right is asking the right
 * question in the right order. A fake that only answers can be satisfied by
 * code that reads the wrong pin or the wrong pull.
 *
 * Every scenario below gives the two readings of a pin DIFFERENT values
 * wherever it can. A test where both readings agree cannot tell a module
 * that reads the pulled-down line from one that reads the floating line, and
 * on 2026-11-02 exactly that sloppiness let three mutants through elsewhere.
 */

#include "wire_probe.h"

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

/* ---- the fake board ------------------------------------------------------ */

#define FAKE_PORTS 3U
#define FAKE_PINS  16U

/* What the pin reads, per port, per pin, per pull. Two separate tables on
   purpose: a pin that reads high alone and low when pulled down is the whole
   point of the module, and one table could not express it. */
static uint32_t g_reads_alone[FAKE_PORTS][FAKE_PINS];
static uint32_t g_reads_pulled[FAKE_PORTS][FAKE_PINS];
/* Set once the chip has pulled a pin down, so the fake remembers that it
   drained the line. A real pin does. */
static uint32_t g_drained[FAKE_PORTS][FAKE_PINS];

/* Every question the module asked, in order. */
#define ASKED_MAX 256U
typedef struct {
    uint8_t port;
    uint8_t pin;
    wire_pull_t pull;
} asked_t;
static asked_t g_asked[ASKED_MAX];
static unsigned g_asked_count;

static uint32_t fake_read(void *ctx, uint8_t port, uint8_t pin,
                          wire_pull_t pull) {
    (void)ctx;
    if (g_asked_count < ASKED_MAX) {
        g_asked[g_asked_count].port = port;
        g_asked[g_asked_count].pin = pin;
        g_asked[g_asked_count].pull = pull;
    }
    g_asked_count++;
    if (port >= FAKE_PORTS || pin >= FAKE_PINS) {
        return 0U;
    }
    if (pull == WIRE_PULL_DOWN) {
        g_drained[port][pin] = 1U;
        return g_reads_pulled[port][pin];
    }
    /* A pin with nothing on it keeps whatever charge it last had, which is
       why it so often reads high on a jumper that goes nowhere. Once the
       chip has pulled it down that charge is gone, so it reads low until
       something outside puts it back. A pin something outside IS holding
       recovers at once, because the outside is driving it.

       This is the fake earning its keep. A stateless table answers the same
       in either order, so it cannot tell a module that reads the floating
       line first from one that reads it after the pull-down has wiped it.
       The order is the whole reason the comment in wire_probe.c exists, and
       a mutant that swapped the two reads survived until this was here. */
    if (g_drained[port][pin] != 0U && g_reads_pulled[port][pin] == 0U) {
        return 0U;
    }
    return g_reads_alone[port][pin];
}

static const wire_bus_t k_bus = {NULL, fake_read};

/* ---- the captured wire --------------------------------------------------- */

#define CAPTURE_MAX 1024U
static char g_lines[CAPTURE_MAX];
static size_t g_len;
static unsigned g_line_count;

static void capture(const char *line) {
    const size_t n = strlen(line);
    if (g_len + n < CAPTURE_MAX) {
        (void)memcpy(&g_lines[g_len], line, n);
        g_len += n;
        g_lines[g_len] = 0;
    }
    g_line_count++;
}

static void setup(void) {
    (void)memset(g_reads_alone, 0, sizeof g_reads_alone);
    (void)memset(g_reads_pulled, 0, sizeof g_reads_pulled);
    (void)memset(g_drained, 0, sizeof g_drained);
    (void)memset(g_asked, 0, sizeof g_asked);
    g_asked_count = 0U;
    g_len = 0U;
    g_lines[0] = 0;
    g_line_count = 0U;
}

static int wire_says(const char *text) {
    return strstr(g_lines, text) != NULL;
}

/* ---- named values -------------------------------------------------------- */

#define PORT_A 0U
#define PORT_B 1U
#define PORT_C 2U

/* The pins the real board uses for the accelerometer, so the scenarios read
   like the thing they were written for. */
#define PIN_SCL 8U
#define PIN_SDA 9U

/* A pin high in one reading and low in the other. Any module that takes its
   verdict from the wrong reading gets this backwards. */
#define HIGH 1U
#define LOW  0U

/* ---- tests --------------------------------------------------------------- */

/* The verdict comes from the pulled-down reading alone. A line that floats
   high and collapses under the internal pull has nothing on it, which is
   exactly what a disconnected jumper looks like. */
static void the_verdict_ignores_the_floating_reading(void) {
    setup();
    g_reads_alone[PORT_B][PIN_SDA] = HIGH;
    g_reads_pulled[PORT_B][PIN_SDA] = LOW;
    CHECK(wire_probe_pin(&k_bus, PORT_B, PIN_SDA) == WIRE_LOOSE);

    setup();
    g_reads_alone[PORT_B][PIN_SCL] = LOW;
    g_reads_pulled[PORT_B][PIN_SCL] = HIGH;
    CHECK(wire_probe_pin(&k_bus, PORT_B, PIN_SCL) == WIRE_HELD);
}

/* Both readings are taken, and the floating one first: reading it after the
   pull-down would show the pull-down leftover instead of the line. */
static void both_readings_are_taken_floating_first(void) {
    setup();
    g_reads_alone[PORT_B][PIN_SDA] = HIGH;
    g_reads_pulled[PORT_B][PIN_SDA] = LOW;
    (void)wire_probe_pin(&k_bus, PORT_B, PIN_SDA);
    CHECK(g_asked_count == 2U);
    CHECK(g_asked[0].pull == WIRE_PULL_NONE);
    CHECK(g_asked[1].pull == WIRE_PULL_DOWN);
    CHECK(g_asked[0].port == PORT_B && g_asked[0].pin == PIN_SDA);
    CHECK(g_asked[1].port == PORT_B && g_asked[1].pin == PIN_SDA);
}

/* The report is two digits in a fixed order, and the pair carries more than
   the verdict: 10 is the signature of a wire that goes nowhere. */
static void the_report_is_two_digits_in_order(void) {
    setup();
    g_reads_alone[PORT_B][PIN_SDA] = HIGH;
    g_reads_pulled[PORT_B][PIN_SDA] = LOW;
    wire_probe_report(&k_bus, "wsda", PORT_B, PIN_SDA, capture);
    CHECK(wire_says("wsda,10"));
    CHECK(g_line_count == 1U);

    setup();
    g_reads_alone[PORT_B][PIN_SCL] = HIGH;
    g_reads_pulled[PORT_B][PIN_SCL] = HIGH;
    wire_probe_report(&k_bus, "wscl", PORT_B, PIN_SCL, capture);
    CHECK(wire_says("wscl,11"));

    /* The one combination a real board rarely shows, kept because the digits
       must be independent: low alone, high when pulled down. */
    setup();
    g_reads_alone[PORT_C][PIN_SDA] = LOW;
    g_reads_pulled[PORT_C][PIN_SDA] = HIGH;
    wire_probe_report(&k_bus, "x", PORT_C, PIN_SDA, capture);
    CHECK(wire_says("x,01"));
}

/* The sweep names what it found, and says it finished even when it found
   nothing. A silent sweep and a sweep that never ran look the same. */
static void the_sweep_names_every_held_pin(void) {
    setup();
    static const wire_port_t ports[] = {
        {'A', PORT_A, 0U}, {'B', PORT_B, 0U}, {'C', PORT_C, 0U}};
    g_reads_pulled[PORT_B][PIN_SCL] = HIGH;
    g_reads_pulled[PORT_C][PIN_SDA] = HIGH;
    /* These two float high and are held by nothing. A sweep that read the
       floating line would report four pins instead of two. */
    g_reads_alone[PORT_A][0U] = HIGH;
    g_reads_alone[PORT_A][15U] = HIGH;

    const uint8_t found = wire_probe_sweep(&k_bus, ports, 3U, capture);
    CHECK(found == 2U);
    CHECK(wire_says("held,B8"));
    CHECK(wire_says("held,C9"));
    CHECK(!wire_says("held,A0"));
    CHECK(!wire_says("held,A15"));
    CHECK(wire_says("held,end"));
}

/* A pin number past nine needs both digits, because held,B1 and held,B15
   would send somebody to different holes. */
static void a_two_digit_pin_keeps_both_digits(void) {
    setup();
    static const wire_port_t ports[] = {{'B', PORT_B, 0U}};
    g_reads_pulled[PORT_B][15U] = HIGH;
    CHECK(wire_probe_sweep(&k_bus, ports, 1U, capture) == 1U);
    CHECK(wire_says("held,B15"));
}

/* Pins with a job are never touched. Reading the UART pair as plain inputs
   would cut the wire this report travels on, so a mask that is ignored does
   not merely report noise, it silences the board. */
static void kept_pins_are_never_even_read(void) {
    setup();
    static const wire_port_t ports[] = {
        {'A', PORT_A, (uint16_t)((1U << 2) | (1U << 3))}};
    g_reads_pulled[PORT_A][2U] = HIGH;
    g_reads_pulled[PORT_A][3U] = HIGH;
    g_reads_pulled[PORT_A][4U] = HIGH;

    CHECK(wire_probe_sweep(&k_bus, ports, 1U, capture) == 1U);
    CHECK(wire_says("held,A4"));
    CHECK(!wire_says("held,A2"));
    CHECK(!wire_says("held,A3"));
    for (unsigned i = 0U; i < g_asked_count; i++) {
        CHECK(g_asked[i].pin != 2U && g_asked[i].pin != 3U);
    }
}

/* An empty sweep still finishes out loud. */
static void an_empty_sweep_still_says_it_finished(void) {
    setup();
    static const wire_port_t ports[] = {{'B', PORT_B, 0U}};
    CHECK(wire_probe_sweep(&k_bus, ports, 1U, capture) == 0U);
    CHECK(wire_says("held,end"));
    CHECK(g_line_count == 1U);
}

/* Nothing is dereferenced that a caller might not have given. */
static void a_missing_bus_or_sink_is_survived(void) {
    setup();
    static const wire_port_t ports[] = {{'B', PORT_B, 0U}};
    static const wire_bus_t empty = {NULL, NULL};
    CHECK(wire_probe_pin(NULL, PORT_B, PIN_SDA) == WIRE_LOOSE);
    CHECK(wire_probe_pin(&empty, PORT_B, PIN_SDA) == WIRE_LOOSE);
    CHECK(wire_probe_sweep(&empty, ports, 1U, capture) == 0U);
    CHECK(wire_probe_sweep(&k_bus, NULL, 1U, capture) == 0U);
    wire_probe_report(&empty, "x", PORT_B, PIN_SDA, capture);
    wire_probe_report(&k_bus, NULL, PORT_B, PIN_SDA, capture);
    g_reads_pulled[PORT_B][PIN_SDA] = HIGH;
    wire_probe_report(&k_bus, "x", PORT_B, PIN_SDA, NULL);
    CHECK(g_line_count == 0U);
}

int main(void) {
    (void)printf("unit: is the wire physically there\n");

    the_verdict_ignores_the_floating_reading();
    both_readings_are_taken_floating_first();
    the_report_is_two_digits_in_order();
    the_sweep_names_every_held_pin();
    a_two_digit_pin_keeps_both_digits();
    kept_pins_are_never_even_read();
    an_empty_sweep_still_says_it_finished();
    a_missing_bus_or_sink_is_survived();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
