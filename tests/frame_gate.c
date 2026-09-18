/**
 * @file frame_gate.c
 * @brief Implementation of the host parser's acceptance rules.
 *
 * Line-oriented and allocation-free, in the same style as the firmware it
 * guards: fixed buffers, explicit lengths, no library parsing helpers.
 */

#include "frame_gate.h"

#define FIELD_DELIMITER '\n'
#define FIELD_SEPARATOR ','
#define CARRIAGE_RETURN  '\r'

/// Copy `len` bytes into a field buffer, NUL-terminating. 0 if it did not fit.
static unsigned copy_field(char *dst, const char *src, size_t len) {
    if (len >= FRAME_GATE_MAX_FIELD) {
        return 0U;
    }
    for (size_t i = 0U; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return 1U;
}

/// Apply the field rules to one line (no terminator). 1 if it was accepted.
static unsigned accept_line(const char *line, size_t len,
                            frame_gate_result_t *out) {
    size_t separators = 0U;
    size_t split      = 0U;

    for (size_t i = 0U; i < len; i++) {
        if (line[i] == FIELD_SEPARATOR) {
            separators++;
            split = i;
        }
    }
    /* Exactly one separator: "noseparator" and "a,b,c" are both rejected. */
    if (separators != 1U) {
        return 0U;
    }

    const size_t id_len    = split;
    const size_t value_len = len - split - 1U;
    /* Neither field may be empty: ",23" and "temp," are both rejected. */
    if ((id_len == 0U) || (value_len == 0U)) {
        return 0U;
    }

    if (out->count >= FRAME_GATE_MAX_READINGS) {
        out->overflow = 1U;
        return 0U;
    }

    frame_gate_reading_t *reading = &out->items[out->count];
    if (!copy_field(reading->sensor_id, line, id_len) ||
        !copy_field(reading->value, &line[split + 1U], value_len)) {
        out->overflow = 1U;
        return 0U;
    }

    out->count++;
    return 1U;
}

void frame_gate_consume(const char *stream, frame_gate_result_t *out) {
    out->count    = 0U;
    out->rejected = 0U;
    out->overflow = 0U;
    out->trailing = 0U;

    size_t start = 0U;
    size_t i     = 0U;

    while (stream[i] != '\0') {
        if (stream[i] == FIELD_DELIMITER) {
            size_t len = i - start;
            /* Tolerate CRLF: drop the '\r' before applying the field rules. */
            if ((len > 0U) && (stream[start + len - 1U] == CARRIAGE_RETURN)) {
                len--;
            }
            /* Empty lines are skipped, and do not count as malformed. */
            if (len > 0U) {
                if (!accept_line(&stream[start], len, out)) {
                    out->rejected++;
                }
            }
            start = i + 1U;
        }
        i++;
    }

    /* Bytes after the last '\n' are an unterminated fragment. The host holds
       them until more arrive, so they yield nothing here. */
    if (i > start) {
        out->trailing = 1U;
    }
}
