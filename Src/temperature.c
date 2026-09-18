/**
 * @file temperature.c
 * @brief Raw ADC counts to tenths of a degree. Pure integer arithmetic, so it
 *        compiles and runs on a host PC for unit testing.
 */

#include "temperature.h"

/// Blank flash reads back as all ones. Never a real calibration value.
#define CAL_BLANK 0xFFFFU

/// The lower calibration point, in tenths of a degree (30.0 C).
#define CAL_LOW_DECI 300

/// The span between the calibration points, in tenths (130 C - 30 C).
#define CAL_SPAN_DECI 1000

/// Full scale of the 12-bit ADC. Any raw reading above this is not a reading.
#define ADC_FULL_SCALE 4095U

/**
 * Upper bound on the VDDA-corrected count. The correction factor is
 * VREFINT_CAL / vrefint_raw, which physically stays near 1 (VDDA is between
 * 1.71 V and 3.6 V, so the factor lands between roughly 0.6 and 1.8). Three
 * times full scale is far outside that and still small enough that
 * CAL_SPAN_DECI * offset cannot overflow an int32_t. Degenerate inputs are
 * rejected here rather than silently producing a number.
 */
#define CORRECTED_MAX (ADC_FULL_SCALE * 3U)

/**
 * Plausibility window for the answer, in tenths of a degree. The L476 sensor
 * is specified across -40 C to 125 C, so anything outside -50 C to 150 C did
 * not come from a working measurement, however arithmetically valid it looks.
 *
 * Without this, in-range-but-degenerate inputs produce numbers like 3668 C:
 * defined, non-overflowing, and complete nonsense. On a dashboard that is
 * worse than no reading, because it looks like data. Out of range is reported
 * as ::TEMPERATURE_INVALID, and the caller omits the frame.
 */
#define DECI_PLAUSIBLE_MIN (-500)
#define DECI_PLAUSIBLE_MAX 1500

uint32_t temperature_cal_usable(const temperature_cal_t *cal) {
    if ((cal->ts_cal1 == CAL_BLANK) || (cal->ts_cal2 == CAL_BLANK) ||
        (cal->vrefint_cal == CAL_BLANK)) {
        return 0U;
    }
    /* A zero reference would divide by zero in the VDDA rescale. */
    if (cal->vrefint_cal == 0U) {
        return 0U;
    }
    /* Equal points give a zero-width span: the line would divide by zero. */
    if (cal->ts_cal2 == cal->ts_cal1) {
        return 0U;
    }
    return 1U;
}

int32_t temperature_deci_celsius(const temperature_cal_t *cal, ts_counts_t ts,
                                 vrefint_counts_t vref) {
    /* Unwrapped once, here. Past this line they are plain numbers again, but
       they arrived correctly labelled and that is the point. */
    const uint32_t ts_raw      = ts.counts;
    const uint32_t vrefint_raw = vref.counts;

    if (!temperature_cal_usable(cal) || (vrefint_raw == 0U)) {
        return TEMPERATURE_INVALID;
    }
    /* Both are 12-bit ADC results. Anything larger is a caller bug, not a
       cold or hot die, and must not be turned into a temperature. */
    if ((ts_raw > ADC_FULL_SCALE) || (vrefint_raw > ADC_FULL_SCALE)) {
        return TEMPERATURE_INVALID;
    }

    /* Rescale the reading from the board's real VDDA to the 3.0 V that the
       calibration assumes. 4095 * 65535 fits in a uint32_t with room spare. */
    const uint32_t ts_corrected =
        (ts_raw * (uint32_t)cal->vrefint_cal) / vrefint_raw;
    if (ts_corrected > CORRECTED_MAX) {
        return TEMPERATURE_INVALID;
    }

    /* Both operands are bounded above, so the multiply below cannot overflow:
       |offset| <= CORRECTED_MAX + 65535, and 1000 * that is well inside
       int32_t. */
    const int32_t span   = (int32_t)cal->ts_cal2 - (int32_t)cal->ts_cal1;
    const int32_t offset = (int32_t)ts_corrected - (int32_t)cal->ts_cal1;
    const int32_t deci   = CAL_LOW_DECI + ((CAL_SPAN_DECI * offset) / span);

    if ((deci < DECI_PLAUSIBLE_MIN) || (deci > DECI_PLAUSIBLE_MAX)) {
        return TEMPERATURE_INVALID;
    }
    return deci;
}
