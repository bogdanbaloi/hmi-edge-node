#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/**
 * @file adc.h
 * @brief Internal temperature-sensor ADC driver (the only place ADC1
 *        registers are touched).
 *
 * The driver reports raw counts and the factory constants. It deliberately
 * does NOT convert to degrees: that is pure arithmetic and lives in
 * `temperature`, where a host test can reach it. `main.c` joins the two.
 */

/// Bring up ADC1 to read the MCU's internal temperature sensor (channel 17)
/// and the internal reference VREFINT (channel 0): leave deep power-down,
/// start the regulator, calibrate, enable, configure both sample times.
void adc_temp_init(void);

/// Run one conversion of the internal temperature sensor. Returns the raw
/// 12-bit ADC counts (0..4095); higher counts mean a warmer die.
uint32_t adc_temp_read(void);

/// Run one conversion of VREFINT. Returns raw 12-bit counts. Needed because
/// the factory calibration assumes VDDA = 3.0 V while a Nucleo runs at 3.3 V.
uint32_t adc_vref_read(void);

/// Factory temperature-sensor calibration at 30 C, from system memory.
uint16_t adc_ts_cal1(void);

/// Factory temperature-sensor calibration at 130 C, from system memory.
uint16_t adc_ts_cal2(void);

/// Factory VREFINT calibration at 30 C and VDDA 3.0 V, from system memory.
uint16_t adc_vrefint_cal(void);

#endif /* ADC_H */
