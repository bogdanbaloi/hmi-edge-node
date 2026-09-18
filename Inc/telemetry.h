#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

/**
 * @file telemetry.h
 * @brief Application logic: turn a button press into telemetry frames.
 *
 * Pure logic -- no registers, no hardware. It depends only on injected
 * function pointers (a temperature reader and a line sink), so it is
 * unit-testable on a host PC with a fake reader and a capturing sink.
 * That is the whole point of splitting it from the HAL.
 */

/// Reads a temperature sample (raw ADC counts). On target: adc_temp_read.
typedef uint32_t (*telemetry_temp_reader_t)(void);

/// Receives one complete line to transmit. On target: uart_send_string.
typedef void (*telemetry_sink_t)(const char *line);

/// Edge-detect + toggle state carried between calls.
typedef struct {
    uint32_t equipment_on;
    uint32_t button_was_down;
} telemetry_state_t;

/// Zero the state (equipment off, button released).
void telemetry_init(telemetry_state_t *state);

/// Call every loop with the current button state. On the press EDGE (was up,
/// now down) it toggles the equipment state and emits, through `sink`:
///     `equipment/0/state,on|off\n`
///     `temp,<raw>\n`          (raw = read_temp())
/// Returns 1 when it acted on an edge (so the caller can update the LED and
/// debounce), 0 otherwise. Never touches hardware directly.
uint32_t telemetry_update(telemetry_state_t *state, uint32_t button_down,
                          telemetry_temp_reader_t read_temp,
                          telemetry_sink_t sink);

#endif /* TELEMETRY_H */
