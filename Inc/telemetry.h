#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

/**
 * @file telemetry.h
 * @brief Application logic: turn a button press into telemetry frames.
 *
 * Pure logic, no registers, no hardware. It depends only on injected
 * function pointers (a temperature reader and a line sink), so it is
 * unit-testable on a host PC with a fake reader and a capturing sink.
 * That is the whole point of splitting it from the HAL.
 */

/// Reads a die temperature in TENTHS of a degree Celsius, or
/// ::TEMPERATURE_INVALID when none can be computed. On target this is the
/// adapter in main.c that reads the ADC and applies the factory calibration.
typedef int32_t (*telemetry_temp_reader_t)(void);

/// Receives one complete line to transmit. On target: uart_send_string.
typedef void (*telemetry_sink_t)(const char *line);

/**
 * How long a press blocks further presses, in milliseconds.
 *
 * A button is two pieces of metal meeting, and they bounce apart several times
 * over the first few milliseconds. The pin shows that as a burst of edges, and
 * a processor polling at megahertz sees every one of them. Thirty milliseconds
 * comfortably outlasts the bounce on the Nucleo's B1 while staying far below
 * the fastest a human can press twice on purpose.
 */
#define TELEMETRY_DEBOUNCE_MS 30U

/// Edge-detect, toggle, and debounce state carried between calls.
typedef struct {
    uint32_t equipment_on;
    uint32_t button_was_down;
    uint32_t last_edge_ms;   ///< When the last accepted press happened.
    uint32_t have_edge;      ///< 0 until the first press, so time zero works.
} telemetry_state_t;

/// Zero the state (equipment off, button released, no press seen yet).
void telemetry_init(telemetry_state_t *state);

/// Call every loop with the current button state and the current time in
/// milliseconds. On the press EDGE (was up, now down) it toggles the equipment
/// state and emits, through `sink`:
///     `equipment/0/state,on|off\n`
///     `temp,<degrees>\n`      (one decimal, e.g. `temp,23.5` or `temp,-4.2`)
///
/// An edge arriving less than ::TELEMETRY_DEBOUNCE_MS after the last accepted
/// one is contact bounce, not a press, and is ignored. Debouncing lives here
/// rather than as a delay in the caller for two reasons: the caller then never
/// has to stall, and a decision about time is pure logic, so a host test can
/// drive it with any clock it likes, including one about to wrap.
///
/// `now_ms` must come from a clock that counts real time. Comparisons are done
/// by subtraction, so a wrapping counter stays correct.
///
/// When `read_temp` returns ::TEMPERATURE_INVALID the temperature frame is
/// SKIPPED entirely: the device reports the state change but does not claim a
/// temperature it could not compute. The state frame is always sent.
///
/// Returns 1 when it acted on an edge (so the caller can update the LED),
/// 0 otherwise. Never touches hardware directly.
uint32_t telemetry_update(telemetry_state_t *state, uint32_t button_down,
                          uint32_t now_ms, telemetry_temp_reader_t read_temp,
                          telemetry_sink_t sink);

#endif /* TELEMETRY_H */
