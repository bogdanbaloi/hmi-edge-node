#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

/**
 * @file temperature.h
 * @brief Turn raw ADC counts into tenths of a degree Celsius.
 *
 * Pure arithmetic, no registers, so it runs in a host test. The driver
 * reads the numbers, this decides what they mean, and `main.c` wires the two
 * together. Keeping the maths here is what makes it checkable without a board.
 *
 * ## Why VREFINT is not optional
 *
 * ST measured all three calibration points at VDDA = 3.0 V. A Nucleo-L476RG
 * runs VDDA at 3.3 V, so the same die temperature produces a ~10% smaller
 * count than the calibration line expects. Applying the line directly would
 * be wrong by tens of degrees, a plausible-looking number that is simply
 * false. Reading VREFINT (a bandgap reference, constant against VDDA) gives
 * the scale factor back:
 *
 *     ts_corrected = ts_raw * VREFINT_CAL / vrefint_raw
 *
 * and the calibration line then applies to `ts_corrected`.
 *
 * ## The maths, in integers
 *
 * No floating point, by choice rather than by necessity. The FPU is enabled at
 * startup (see core_enable_fpu), so a float would work, but a tenth of a
 * degree is already finer than the sensor's accuracy. Everything is `int32_t`,
 * and the result is in TENTHS of a degree, which is what the wire format wants
 * (`temp,23.5`).
 *
 *     deci = 300 + (1000 * (ts_corrected - TS_CAL1)) / (TS_CAL2 - TS_CAL1)
 *
 * 300 is 30.0 C, the lower calibration point. 1000 is the 100 C span between
 * the two points, in tenths.
 */

/// Returned when no honest temperature can be computed. Chosen outside any
/// physical range so it cannot be mistaken for a reading. Callers must not
/// report it as a temperature.
#define TEMPERATURE_INVALID INT32_MIN

/// The three factory constants, as read from system memory.
typedef struct {
    uint16_t ts_cal1;      ///< Temperature-sensor raw counts at 30 C.
    uint16_t ts_cal2;      ///< Temperature-sensor raw counts at 130 C.
    uint16_t vrefint_cal;  ///< VREFINT raw counts at 30 C.
} temperature_cal_t;

/**
 * Raw counts from the temperature-sensor channel, in their own type.
 *
 * ## Why a struct around one number
 *
 * The conversion needs two readings, both `uint32_t`, both in the same range.
 * Passed as bare integers they sit side by side in the signature, and swapping
 * them at the call site compiles cleanly and returns a temperature that looks
 * entirely reasonable. Nothing catches it: not the compiler, not clang-tidy
 * (checked), and not the tests, because the mistake would live in main.c,
 * which no host test reaches.
 *
 * That is the worst failure class in this firmware: silent, plausible, and
 * pointed at a dashboard. The rest of the piece treats that class by making
 * the mistake impossible rather than detectable, and this is the same move in
 * the type system. Swap these two now and it does not compile.
 *
 * The wrapping happens in main.c, not in `adc`. Having the driver return these
 * types would read better at the call site but would make the HAL depend on
 * application code, inverting the layering. The composition root is where the
 * driver and the maths already meet, so it is where the labelling belongs.
 */
typedef struct {
    uint32_t counts;
} ts_counts_t;

/// Raw counts from the VREFINT channel. Distinct from ::ts_counts_t on
/// purpose: see the note there.
typedef struct {
    uint32_t counts;
} vrefint_counts_t;

/**
 * @brief Is this calibration block usable at all?
 *
 * Guards the two divisions below AND catches blank flash (0xFFFF), which is
 * what a bad address or an unprogrammed part reads back as. Without this a
 * blank block would divide by zero.
 *
 * @return 1 when the constants can be used, 0 otherwise.
 */
uint32_t temperature_cal_usable(const temperature_cal_t *cal);

/**
 * @brief Die temperature in tenths of a degree Celsius.
 * @param cal   Factory constants. Never NULL.
 * @param ts    Counts from the temperature-sensor channel.
 * @param vref  Counts from the VREFINT channel, for the VDDA scale.
 * @return Tenths of a degree, or ::TEMPERATURE_INVALID when no honest answer
 *         exists: unusable calibration, a zero or out-of-scale reading, or a
 *         result outside the sensor's plausible range (-50 C to 150 C).
 *         Truncates toward zero.
 *
 * The two readings carry distinct types, so passing them the wrong way round
 * is a compile error rather than a plausible wrong number. See ::ts_counts_t.
 */
int32_t temperature_deci_celsius(const temperature_cal_t *cal, ts_counts_t ts,
                                 vrefint_counts_t vref);

#endif /* TEMPERATURE_H */
