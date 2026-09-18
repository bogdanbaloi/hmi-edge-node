/**
 * @file telemetry.c
 * @brief Pure application logic. No registers here, so it compiles and runs
 *        on a host PC for unit testing (fake reader + capturing sink).
 */

#include "telemetry.h"

#include "temperature.h"

#define DECIMAL_BASE 10U
#define UINT32_MAX_DIGITS 10U  /* 4294967295 */
/* "temp," + sign + digits + "." + one decimal + "\n\0" */
#define TEMP_LINE_MAX (UINT32_MAX_DIGITS + 12U)

void telemetry_init(telemetry_state_t *state) {
    state->equipment_on    = 0U;
    state->button_was_down = 0U;
    state->last_edge_ms    = 0U;
    state->have_edge       = 0U;
}

/// Format an unsigned value as decimal into `out` (>= 11 chars), returning a
/// pointer to the first digit. No printf on bare metal.
static const char *format_uint(uint32_t value, char *out) {
    uint32_t i = UINT32_MAX_DIGITS + 1U;  /* the NUL slot */
    out[i] = '\0';
    do {
        out[--i] = (char)('0' + (value % DECIMAL_BASE));
        value /= DECIMAL_BASE;
    } while (value != 0U);
    return &out[i];
}

/// Magnitude of a signed value, without overflowing on INT32_MIN (where
/// plain negation is undefined).
static uint32_t absolute(int32_t value) {
    if (value < 0) {
        return (uint32_t)(-(value + 1)) + 1U;
    }
    return (uint32_t)value;
}

/// Build the whole "temp,<degrees>\n" line into `line`, one decimal place.
/// `deci` is in tenths of a degree and may be negative.
static void build_temp_line(int32_t deci, char *line) {
    char digits[UINT32_MAX_DIGITS + 2U];
    const uint32_t magnitude = absolute(deci);
    const char *prefix = "temp,";
    uint32_t p = 0U;

    while (*prefix != '\0') {
        line[p++] = *prefix++;
    }
    /* -0.4 must keep its sign: the whole part alone would read as zero. */
    if (deci < 0) {
        line[p++] = '-';
    }

    const char *whole = format_uint(magnitude / DECIMAL_BASE, digits);
    while (*whole != '\0') {
        line[p++] = *whole++;
    }

    line[p++] = '.';
    line[p++] = (char)('0' + (magnitude % DECIMAL_BASE));
    line[p++] = '\n';
    line[p]   = '\0';
}

/**
 * Is this edge a real press, or the contacts still bouncing?
 *
 * The comparison is a SUBTRACTION, deliberately. Written as
 * `now_ms >= state->last_edge_ms + TELEMETRY_DEBOUNCE_MS` it would break when
 * the clock wraps: the sum overflows, the condition goes false, and the button
 * stops responding until the counter comes round again. Unsigned subtraction
 * has no such hole, because the wrap cancels out.
 */
static uint32_t press_is_settled(const telemetry_state_t *state,
                                 uint32_t now_ms) {
    if (!state->have_edge) {
        return 1U;  /* first press ever, nothing to bounce off */
    }
    return ((now_ms - state->last_edge_ms) >= TELEMETRY_DEBOUNCE_MS) ? 1U : 0U;
}

uint32_t telemetry_update(telemetry_state_t *state, uint32_t button_down,
                          uint32_t now_ms, telemetry_temp_reader_t read_temp,
                          telemetry_sink_t sink) {
    uint32_t acted = 0U;

    if (button_down && !state->button_was_down && press_is_settled(state, now_ms)) {
        state->last_edge_ms = now_ms;
        state->have_edge    = 1U;
        state->equipment_on = !state->equipment_on;

        sink(state->equipment_on ? "equipment/0/state,on\n"
                                 : "equipment/0/state,off\n");

        /* Report a temperature only when there is one. A sentinel on the wire
           would be a number the host cannot tell apart from a reading. */
        const int32_t deci = read_temp();
        if (deci != TEMPERATURE_INVALID) {
            char line[TEMP_LINE_MAX];
            build_temp_line(deci, line);
            sink(line);
        }

        acted = 1U;
    }
    state->button_was_down = button_down;
    return acted;
}
