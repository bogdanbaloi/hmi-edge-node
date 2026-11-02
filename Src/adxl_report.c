/**
 * @file adxl_report.c
 * @brief Pure application logic. No registers and no bus here, so it compiles
 *        and runs on a host PC for unit testing.
 */

#include "adxl_report.h"

void adxl_report_init(adxl_report_state_t *state) {
    state->last_sent_ms = 0U;
    state->have_sent    = 0U;
    state->brought_up   = 0U;
    state->presence     = ADXL_PRESENCE_ABSENT;
}

/// The line that states a presence.
///
/// Absent is the fallback on purpose: a value this enum does not name must
/// not be able to claim the sensor is working. No test reaches that branch,
/// because reaching it means casting a number the enum does not name, so it
/// is defence rather than behaviour. What IS tested is that no named value
/// other than READY ever produces the ready line.
static const char *line_for(adxl_presence_t presence) {
    switch (presence) {
    case ADXL_PRESENCE_READY:
        return ADXL_LINE_READY;
    case ADXL_PRESENCE_WRONG:
        return ADXL_LINE_WRONG;
    case ADXL_PRESENCE_ABSENT:
    default:
        return ADXL_LINE_ABSENT;
    }
}

/**
 * Has the period passed since the last line?
 *
 * The comparison is a SUBTRACTION, for the same reason telemetry debounces by
 * subtraction: written as `now_ms >= last + PERIOD` the sum overflows near the
 * end of the counter, the condition goes false, and the status goes quiet
 * until the clock comes round. Unsigned subtraction has no such hole.
 */
static uint32_t period_has_passed(const adxl_report_state_t *state,
                                  uint32_t now_ms) {
    if (!state->have_sent) {
        return 1U;  /* nothing sent yet, so speak at once */
    }
    return ((now_ms - state->last_sent_ms) >= ADXL_REPORT_PERIOD_MS) ? 1U : 0U;
}

uint32_t adxl_report_update(adxl_report_state_t *state, uint32_t now_ms,
                            adxl_report_probe_t probe,
                            adxl_report_sink_t sink) {
    if (!period_has_passed(state, now_ms)) {
        return 0U;
    }

    /* Probed again on every period while it is not up, so a sensor that gets
       wired correctly on a running board starts working without a reset. Once
       it is up the probe is never called again: it writes configuration
       registers, and rewriting them restarts the sensor's FIFO. */
    if (!state->brought_up) {
        state->presence = probe();
        if (state->presence == ADXL_PRESENCE_READY) {
            state->brought_up = 1U;
        }
    }

    state->last_sent_ms = now_ms;
    state->have_sent    = 1U;
    sink(line_for(state->presence));
    return 1U;
}
