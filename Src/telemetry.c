/**
 * @file telemetry.c
 * @brief Pure application logic. No registers here, so it compiles and runs
 *        on a host PC for unit testing (fake reader + capturing sink).
 */

#include "telemetry.h"

#define DECIMAL_BASE 10U
#define UINT32_MAX_DIGITS 10U  /* 4294967295 */
#define TEMP_LINE_MAX (UINT32_MAX_DIGITS + 8U)  /* "temp," + digits + "\n\0" */

void telemetry_init(telemetry_state_t *state) {
    state->equipment_on    = 0U;
    state->button_was_down = 0U;
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

/// Build the whole "temp,<raw>\n" line into `line`.
static void build_temp_line(uint32_t raw, char *line) {
    char digits[UINT32_MAX_DIGITS + 2U];
    const char *d = format_uint(raw, digits);
    const char *prefix = "temp,";
    uint32_t p = 0U;
    while (*prefix != '\0') {
        line[p++] = *prefix++;
    }
    while (*d != '\0') {
        line[p++] = *d++;
    }
    line[p++] = '\n';
    line[p]   = '\0';
}

uint32_t telemetry_update(telemetry_state_t *state, uint32_t button_down,
                          telemetry_temp_reader_t read_temp,
                          telemetry_sink_t sink) {
    uint32_t acted = 0U;

    if (button_down && !state->button_was_down) {
        state->equipment_on = !state->equipment_on;

        sink(state->equipment_on ? "equipment/0/state,on\n"
                                 : "equipment/0/state,off\n");

        char line[TEMP_LINE_MAX];
        build_temp_line(read_temp(), line);
        sink(line);

        acted = 1U;
    }
    state->button_was_down = button_down;
    return acted;
}
