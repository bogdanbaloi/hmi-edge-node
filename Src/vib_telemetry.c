/**
 * @file vib_telemetry.c
 * @brief Pure application logic. No registers and no device header, so it
 *        compiles and runs on a host PC for unit testing.
 */

#include "vib_telemetry.h"

#define DECIMAL_BASE 10U
/* 4294967295 is ten digits, and the biggest figure this can ever report is
   four of them, so the room is generous rather than calculated. */
#define UINT32_MAX_DIGITS 10U
/* "vib," + digits + "\n" + NUL */
#define VIB_LINE_MAX (UINT32_MAX_DIGITS + 6U)

/**
 * Append a decimal number, returning the new end of the string.
 *
 * A second decimal formatter in this firmware, telemetry.c having the first.
 * They are left apart on purpose for now: telemetry's writes backwards into a
 * buffer and hands back a pointer to the first digit, while this one appends,
 * so sharing them means changing the module whose output is pinned by a
 * contract test against the host. That is worth its own piece rather than a
 * detour inside this one.
 */
static char *append_uint(char *out, uint32_t value) {
    char     digits[UINT32_MAX_DIGITS];
    uint32_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % DECIMAL_BASE));
        value /= DECIMAL_BASE;
    } while (value != 0U);

    while (count > 0U) {
        *out++ = digits[--count];
    }
    return out;
}

/// Build the whole `vib,<milli-g>\n` line. Same shape as every other frame:
/// a sensor id, a comma, a value, a newline, and nothing else.
static void build_line(uint32_t milli_g, char *line) {
    const char *prefix = "vib,";
    char       *p      = line;

    while (*prefix != '\0') {
        *p++ = *prefix++;
    }
    p    = append_uint(p, milli_g);
    *p++ = '\n';
    *p   = '\0';
}

void vib_telemetry_init(vib_telemetry_t *stream, uint32_t milli_g_per_count) {
    vibration_init(&stream->window, milli_g_per_count);
    stream->last_poll_ms = 0U;
    stream->have_polled  = 0U;
}

/**
 * Has the poll period passed?
 *
 * Subtraction, not addition, for the same reason telemetry debounces by
 * subtraction: the sum overflows near the end of a 32 bit millisecond
 * counter, the condition goes false, and the stream goes quiet until the
 * clock comes round again.
 */
static uint32_t period_has_passed(const vib_telemetry_t *stream,
                                  uint32_t now_ms) {
    if (!stream->have_polled) {
        return 1U;
    }
    return ((now_ms - stream->last_poll_ms) >= VIB_POLL_PERIOD_MS) ? 1U : 0U;
}

/// Throw the window away and start a new one. Used whenever the samples
/// collected so far are not known to be consecutive.
static void start_over(vib_telemetry_t *stream) {
    vibration_init(&stream->window, stream->window.milli_g_per_count);
}

uint32_t vib_telemetry_poll(vib_telemetry_t *stream, uint32_t now_ms,
                            const vib_sensor_t *sensor, vib_sink_t sink) {
    if (!period_has_passed(stream, now_ms)) {
        return 0U;
    }
    stream->last_poll_ms = now_ms;
    stream->have_polled  = 1U;

    uint8_t waiting = 0U;
    if (sensor->fifo_level(sensor->ctx, &waiting) != 0) {
        start_over(stream);
        return 0U;
    }
    if (waiting >= ADXL345_FIFO_DEPTH_ASSUMED) {
        /* Full means the sensor had nowhere to put the next sample, so the
           ones still in there are probably not consecutive with what this
           window already holds. */
        start_over(stream);
        return 0U;
    }

    uint32_t sent = 0U;
    for (uint8_t taken = 0U; taken < waiting; taken++) {
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
        if (sensor->read_sample(sensor->ctx, &x, &y, &z) != 0) {
            start_over(stream);
            return sent;
        }

        uint32_t milli_g = 0U;
        if (vibration_push(&stream->window, x, y, z, &milli_g) != 0U) {
            char line[VIB_LINE_MAX];
            build_line(milli_g, line);
            sink(line);
            sent++;
        }
    }
    return sent;
}
