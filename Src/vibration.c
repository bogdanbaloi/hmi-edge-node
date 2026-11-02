/**
 * @file vibration.c
 * @brief Pure arithmetic. No registers and no device, so it compiles and runs
 *        on a host PC for unit testing.
 */

#include "vibration.h"

/// Bit pairs are consumed two at a time, so the search starts at the highest
/// EVEN power of two that a 64 bit value can hold.
#define ISQRT_FIRST_BIT_SHIFT 62U
/// One square root digit per two bits of the operand.
#define ISQRT_BITS_PER_DIGIT  2U

/**
 * @brief Integer square root, exact for perfect squares.
 *
 * Digit by digit in base four, which is long division's square root: no
 * floating point, no iteration count to tune and no convergence to argue
 * about. The result is the floor of the true root.
 *
 * Why not sqrt() from the library: this runs on a Cortex-M4 with no operating
 * system, where pulling in the maths library for one call costs flash and
 * drags in error handling that has nowhere to report to.
 */
static uint32_t integer_square_root(uint64_t value) {
    uint64_t remainder = value;
    uint64_t root      = 0U;
    uint64_t bit       = (uint64_t)1U << ISQRT_FIRST_BIT_SHIFT;

    while (bit > remainder) {
        bit >>= ISQRT_BITS_PER_DIGIT;
    }
    while (bit != 0U) {
        if (remainder >= (root + bit)) {
            remainder -= root + bit;
            root       = (root >> 1U) + bit;
        } else {
            root >>= 1U;
        }
        bit >>= ISQRT_BITS_PER_DIGIT;
    }
    return (uint32_t)root;
}

void vibration_init(vibration_t *window, uint32_t milli_g_per_count) {
    window->milli_g_per_count = milli_g_per_count;
    window->samples           = 0U;
    window->sum_x             = 0;
    window->sum_y             = 0;
    window->sum_z             = 0;
    window->sum_of_squares    = 0U;
}

/// The square of a sample, widened before multiplying so a 16 bit value
/// cannot overflow on the way.
static uint64_t squared(int16_t sample) {
    const int64_t wide = (int64_t)sample;
    return (uint64_t)(wide * wide);
}

/**
 * @brief The root mean square of the window with its mean removed, in counts
 *        scaled up by the sample count.
 *
 * Computed as `N * sum_of_squares - (sum)^2` rather than by subtracting a
 * mean from every sample. The two are the same quantity, but the mean of a
 * window dominated by gravity is not a whole number, and rounding it would
 * put an error of up to half a count into EVERY sample before squaring. The
 * vibration being measured can itself be a count or two, so that error is not
 * a rounding detail, it is the signal.
 *
 * The identity is `N * sum((x - mean)^2) == N * sum(x^2) - (sum x)^2`, summed
 * over the three axes because variance adds across perpendicular directions.
 * It is never negative: that is Cauchy's inequality, not an assumption.
 */
static uint64_t scaled_variance(const vibration_t *window) {
    const int64_t sx = window->sum_x;
    const int64_t sy = window->sum_y;
    const int64_t sz = window->sum_z;
    const uint64_t squares_of_sums =
        (uint64_t)((sx * sx) + (sy * sy) + (sz * sz));

    return ((uint64_t)VIBRATION_WINDOW_SAMPLES * window->sum_of_squares)
           - squares_of_sums;
}

uint32_t vibration_push(vibration_t *window, int16_t x, int16_t y, int16_t z,
                        uint32_t *out_milli_g) {
    window->sum_x          += x;
    window->sum_y          += y;
    window->sum_z          += z;
    window->sum_of_squares += squared(x) + squared(y) + squared(z);
    window->samples++;

    if (window->samples < VIBRATION_WINDOW_SAMPLES) {
        return 0U;
    }

    /* The root of the scaled variance is the RMS multiplied by the sample
       count, so the scale is applied BEFORE dividing it back out. Dividing
       first would throw away everything below one count, which is 4 mg on
       this sensor and more than some of the readings being asked for. */
    const uint64_t scaled_rms = integer_square_root(scaled_variance(window));
    *out_milli_g = (uint32_t)((scaled_rms * window->milli_g_per_count)
                              / VIBRATION_WINDOW_SAMPLES);

    vibration_init(window, window->milli_g_per_count);
    return 1U;
}
