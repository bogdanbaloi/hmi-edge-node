#ifndef VIBRATION_H
#define VIBRATION_H

#include <stdint.h>

/**
 * @file vibration.h
 * @brief Application logic: turn a stream of accelerometer samples into one
 *        vibration figure, in milli-g.
 *
 * Pure arithmetic. No registers, no bus and no device header, so it compiles
 * and runs on a host PC against samples a test makes up. The sensor's scale
 * is injected at init rather than written in here, because it belongs to the
 * sensor and its configuration, not to the maths.
 *
 * ### What it measures
 *
 * The root mean square of the ALTERNATING part of the acceleration, with the
 * steady part removed. The steady part is gravity plus whatever offset the
 * part has, and neither is vibration: a sensor sitting still on a desk reads
 * about 1000 mg downwards forever, and reporting that as vibration would
 * drown the thing being measured.
 *
 * Removing it costs nothing extra: the mean over the window IS the steady
 * part, so subtracting it removes gravity and the offset in one step, and
 * never needs to know which way is down. A sensor glued to a fan at any
 * angle works with no calibration.
 *
 * ### What it cannot see
 *
 * A window is ::VIBRATION_WINDOW_SAMPLES samples. At the 800 Hz the sensor is
 * configured for that is 200 ms, so a vibration slower than 5 Hz does not
 * complete a cycle inside the window and the mean absorbs part of it. The
 * reading is then too low rather than too high. A fan turning at 1200 rpm is
 * 20 Hz, four cycles per window, which is well clear of that floor.
 *
 * It is an AMPLITUDE and nothing else. An unbalanced blade and a failing
 * bearing can give the same number, and only their FREQUENCY tells them
 * apart, which needs a transform this module does not do.
 */

/**
 * Samples per window.
 *
 * 160 at 800 Hz is exactly 200 ms, so a result comes out five times a second
 * and the telemetry rate falls out of the arithmetic instead of needing a
 * timer. 256 samples would have been the natural choice for a frequency
 * transform later, which wants a power of two, but at 800 Hz it gives 320 ms
 * and an awkward 3.1 results a second.
 */
#define VIBRATION_WINDOW_SAMPLES 160U

/// Accumulated window. Opaque in use: push samples, read results.
typedef struct {
    uint32_t milli_g_per_count;  ///< The sensor's scale, injected at init.
    uint32_t samples;            ///< How many samples this window holds.
    int32_t  sum_x;              ///< Running sum, for the steady part.
    int32_t  sum_y;
    int32_t  sum_z;
    uint64_t sum_of_squares;     ///< Running sum of x*x + y*y + z*z.
} vibration_t;

/// Start an empty window. `milli_g_per_count` is the sensor's full-resolution
/// scale: 4 for an ADXL345 in full resolution, at any range.
void vibration_init(vibration_t *window, uint32_t milli_g_per_count);

/**
 * @brief Add one three-axis sample.
 *
 * Returns 1 exactly when the sample completes a window, having written the
 * result in milli-g to `out_milli_g` and started the next window. Returns 0
 * otherwise and leaves `out_milli_g` alone.
 *
 * Counts, not milli-g, go in: the conversion happens once at the end, where
 * it costs one multiplication instead of ::VIBRATION_WINDOW_SAMPLES of them
 * and loses nothing to rounding on the way.
 */
uint32_t vibration_push(vibration_t *window, int16_t x, int16_t y, int16_t z,
                        uint32_t *out_milli_g);

#endif /* VIBRATION_H */
