#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/**
 * @file adc.h
 * @brief Internal temperature-sensor ADC driver (the only place ADC1
 *        registers are touched).
 */

/// Bring up ADC1 to read the MCU's internal temperature sensor (channel 17):
/// leave deep power-down, start the regulator, calibrate, enable, configure.
void adc_temp_init(void);

/// Run one conversion of the internal temperature sensor. Returns the raw
/// 12-bit ADC counts (0..4095); higher counts mean a warmer die.
uint32_t adc_temp_read(void);

#endif /* ADC_H */
