#ifndef FRAME_GATE_H
#define FRAME_GATE_H

#include <stddef.h>

/**
 * @file frame_gate.h
 * @brief The host parser's acceptance rules, transcribed for the contract test.
 *
 * industrial-hmi turns the serial byte stream into `sensorId,value` readings.
 * This is a deliberately small re-statement of the rules its SerialFrameParser
 * enforces, so that hmi-edge-node can check its own output against them
 * WITHOUT depending on that repo. The two repos stay independent: they share a
 * wire protocol, not code.
 *
 * The rules, each pinned by a sample copied from the host's
 * SerialFrameParserTest (see frame_gate_selfcheck in the test):
 *   - a reading is one line terminated by `\n`; a trailing `\r` is tolerated
 *   - the line holds exactly ONE ',', so zero or two or more is rejected
 *   - neither the sensor id nor the value may be empty
 *   - empty lines are skipped, and a malformed line never drops a valid one
 *   - the value stays TEXT; the host does not parse it as a number
 *
 * Deliberately NOT modelled: reassembling a frame split across two reads, and
 * the runaway-buffer guard. Those are properties of the host's buffering, not
 * of the bytes this firmware puts on the wire, so they are out of contract
 * scope here. A trailing fragment with no `\n` is simply not reported.
 */

/// Readings a single check can hold. The firmware emits two lines per press.
#define FRAME_GATE_MAX_READINGS 8U

/// Field capacity, including the NUL. Generous next to `equipment/0/state`.
#define FRAME_GATE_MAX_FIELD 64U

/// One accepted reading: the `sensorId,value` pair, both as text.
typedef struct {
    char sensor_id[FRAME_GATE_MAX_FIELD];
    char value[FRAME_GATE_MAX_FIELD];
} frame_gate_reading_t;

/// Everything a byte stream yielded, plus the flags a test asserts on.
typedef struct {
    frame_gate_reading_t items[FRAME_GATE_MAX_READINGS];  ///< Accepted readings.
    size_t   count;      ///< How many of `items` are populated.
    size_t   rejected;   ///< Lines seen and thrown away as malformed.
    unsigned overflow;   ///< 1 if a reading or a field did not fit.
    unsigned trailing;   ///< 1 if the stream ended mid-line (no final `\n`).
} frame_gate_result_t;

/**
 * @brief Run a complete byte stream through the host's acceptance rules.
 * @param stream NUL-terminated bytes exactly as they would reach the wire.
 * @param out    Result, fully overwritten. Never NULL.
 */
void frame_gate_consume(const char *stream, frame_gate_result_t *out);

#endif /* FRAME_GATE_H */
