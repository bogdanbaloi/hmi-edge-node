#ifndef VIB_TELEMETRY_H
#define VIB_TELEMETRY_H

#include "vibration.h"

#include <stdint.h>

/**
 * @file vib_telemetry.h
 * @brief Application logic: drain the accelerometer's queue on a schedule and
 *        put one vibration figure on the wire when a window fills.
 *
 * Pure logic. The sensor is injected as two function pointers and the wire as
 * a sink, so a host test drives the whole thing with a fake queue and any
 * clock it likes. It never includes the device header: a different
 * accelerometer needs a different adapter in the composition root and nothing
 * here.
 *
 * ### Why it polls on a clock instead of every loop
 *
 * The main loop runs at about twenty thousand turns a second, measured on the
 * board on 2026-11-02. Asking the sensor how full its queue is on every turn
 * would put twenty thousand transactions a second on a bus that carries a
 * hundred thousand bits a second, so the question alone would fill it.
 *
 * The other end is fixed by the sensor: its queue holds
 * ::ADXL345_FIFO_DEPTH_ASSUMED samples and it fills one every 1.25 ms at the
 * 800 Hz it is configured for, so the queue overflows after 40 ms. Polling at
 * ::VIB_POLL_PERIOD_MS leaves a factor of two in hand.
 *
 * ### What it refuses to do
 *
 * Never a sentinel value. A silent sensor produces NO `vib` line at all,
 * because a number on the wire is one the host cannot tell apart from a
 * reading. Whether the sensor is there is said separately and repeatedly by
 * adxl_report.
 */

/// How often the sensor's queue is drained, in milliseconds. Half the time
/// the queue takes to overflow, so one missed turn costs nothing.
#define VIB_POLL_PERIOD_MS 20U

/// The depth this module assumes, which must match what the driver configured.
/// A queue reading FULL is the signal that samples were probably lost.
#define ADXL345_FIFO_DEPTH_ASSUMED 32U

/// Reads how many samples are waiting. Returns 0 on success.
typedef int (*vib_fifo_level_t)(void *ctx, uint8_t *out_entries);

/// Reads the oldest sample out of the queue. Returns 0 on success.
typedef int (*vib_read_sample_t)(void *ctx, int16_t *x, int16_t *y,
                                 int16_t *z);

/// The sensor, as the two questions this module asks it.
typedef struct {
    void             *ctx;
    vib_fifo_level_t  fifo_level;
    vib_read_sample_t read_sample;
} vib_sensor_t;

/// Receives one complete line to transmit. On target: uart_send_string.
typedef void (*vib_sink_t)(const char *line);

/// Window and schedule carried between calls.
typedef struct {
    vibration_t window;
    uint32_t    last_poll_ms;
    uint32_t    have_polled;  ///< 0 until the first poll, so time zero works.
} vib_telemetry_t;

/// Start empty. `milli_g_per_count` is the sensor's scale, passed straight
/// through to vibration_init().
void vib_telemetry_init(vib_telemetry_t *stream, uint32_t milli_g_per_count);

/**
 * @brief Call every loop with the current time in milliseconds.
 *
 * Does nothing until ::VIB_POLL_PERIOD_MS has passed. Then it asks how many
 * samples are waiting and takes them, and emits `vib,<milli-g>\n` through
 * `sink` on the sample that completes a window.
 *
 * **The window is thrown away, and nothing is sent, when the samples in it
 * are not known to be consecutive.** That happens when the bus fails partway
 * through and when the queue is found FULL, since full means the sensor may
 * already have dropped what did not fit. A root mean square over 200 ms with
 * a hole in it is not a root mean square over 200 ms, and reporting it as one
 * would be the sentinel this module refuses to send.
 *
 * `now_ms` must come from a clock that counts real time. Comparisons are done
 * by subtraction, so a wrapping counter stays correct.
 *
 * Returns 1 when it put a line on the wire, 0 otherwise.
 */
uint32_t vib_telemetry_poll(vib_telemetry_t *stream, uint32_t now_ms,
                            const vib_sensor_t *sensor, vib_sink_t sink);

#endif /* VIB_TELEMETRY_H */
