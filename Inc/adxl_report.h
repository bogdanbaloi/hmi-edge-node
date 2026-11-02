#ifndef ADXL_REPORT_H
#define ADXL_REPORT_H

#include <stdint.h>

/**
 * @file adxl_report.h
 * @brief Application logic: say on the wire whether the accelerometer is
 *        there, repeatedly, and bring it up the first time it answers.
 *
 * Pure logic, no registers and no bus. The probe and the line sink are
 * injected, so a host test drives it with a fake probe and a capturing sink
 * and any clock it likes. Sibling of telemetry.c, same shape and the same
 * reasons.
 *
 * It replaced a single line sent once at start-up. That line was unobservable
 * in practice: on 2026-11-02 the serial monitor opened a few milliseconds late
 * and caught `adx` instead of `adxl,absent`, and a later reset produced
 * nothing at all because the debugger held the core. A status that is only
 * true for one millisecond cannot be read by a person, and a demo is read by
 * a person.
 */

/// What answered at the accelerometer's address, if anything.
typedef enum {
    ADXL_PRESENCE_READY = 0,  ///< An ADXL345 answered and was brought up.
    ADXL_PRESENCE_WRONG,      ///< Something answered, and it is not an ADXL345.
    ADXL_PRESENCE_ABSENT      ///< Nothing acknowledged the address at all.
} adxl_presence_t;

/// Identifies the device and, only when it is the right one, configures it.
/// On target this is the adapter in main.c over adxl345_begin(). Called again
/// on every period until it reports ::ADXL_PRESENCE_READY, so rewiring the
/// sensor on a running board recovers without a reset. Never called again
/// afterwards: it writes configuration registers, and rewriting them would
/// restart the sensor's FIFO under whoever is reading it.
typedef adxl_presence_t (*adxl_report_probe_t)(void);

/// Receives one complete line to transmit. On target: uart_send_string.
typedef void (*adxl_report_sink_t)(const char *line);

/**
 * How often the status is repeated, in milliseconds.
 *
 * Two seconds is a compromise between two readers. A person who has just
 * opened the serial monitor should not have to wait or guess, which argues
 * for less. The button frames share this wire and a status line every
 * hundred milliseconds would bury them, which argues for more. Two seconds
 * also costs nothing while the sensor is missing: one register read.
 */
#define ADXL_REPORT_PERIOD_MS 2000U

/// The three lines, spelled once. The host splits at the first comma, so the
/// part before it is the sensor id and the part after it is the value.
#define ADXL_LINE_READY  "adxl,ready\n"
#define ADXL_LINE_WRONG  "adxl,wrong\n"
#define ADXL_LINE_ABSENT "adxl,absent\n"

/// Timing and probe state carried between calls.
typedef struct {
    uint32_t        last_sent_ms;  ///< When the last status line went out.
    uint32_t        have_sent;     ///< 0 until the first line, so time zero works.
    uint32_t        brought_up;    ///< 1 once the probe has reported READY.
    adxl_presence_t presence;      ///< What the probe last answered.
} adxl_report_state_t;

/// Zero the state: nothing probed, nothing sent, so the first call speaks.
void adxl_report_init(adxl_report_state_t *state);

/// Call every loop with the current time in milliseconds.
///
/// Does nothing until ::ADXL_REPORT_PERIOD_MS has passed since the last line.
/// Then, unless the sensor has already been brought up, it calls `probe` and
/// keeps its answer, and either way it emits one line through `sink`.
///
/// `now_ms` must come from a clock that counts real time. Comparisons are
/// done by subtraction, so a wrapping counter stays correct.
///
/// Returns 1 when it emitted a line, 0 otherwise.
uint32_t adxl_report_update(adxl_report_state_t *state, uint32_t now_ms,
                            adxl_report_probe_t probe,
                            adxl_report_sink_t sink);

#endif /* ADXL_REPORT_H */
