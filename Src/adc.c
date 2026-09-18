/**
 * @file adc.c
 * @brief Internal temperature-sensor ADC driver. The L476 ADC resets in deep
 *        power-down, so the bring-up order matters (see adc_temp_init).
 *
 * Two internal channels are used: 17 (temperature sensor) and 0 (VREFINT).
 * They share one ADC, so each read selects its own channel in the sequence
 * register before starting a conversion.
 */

#include "adc.h"
#include "registers.h"

/// SMPR field 111 = 640.5 cycles. Both internal channels are high-impedance
/// and need a long sample time or the reading is wrong.
#define ADC_SAMPLE_640CYC 7UL
/// > 20us regulator start-up at 4 MHz, as a simple nop-loop count.
#define ADC_VREG_STARTUP_LOOPS 4000UL
/// Each channel owns a 3-bit field in its SMPR register.
#define ADC_SMPR_FIELD_BITS 3U
/// SQ1 sits at bits [10:6] of SQR1.
#define ADC_SQR1_SQ1_SHIFT 6U

static void adc_busy_wait(volatile uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

/// Convert one channel: select it as the whole sequence, start, wait, read.
static uint32_t adc_convert(uint32_t channel) {
    /* One conversion of `channel`: L=0 (one conversion), SQ1 = channel. */
    ADC1_SQR1 = (channel << ADC_SQR1_SQ1_SHIFT);

    ADC1_CR |= ADC_CR_ADSTART;
    while ((ADC1_ISR & ADC_ISR_EOC) == 0U) {
    }
    return ADC1_DR;
}

void adc_temp_init(void) {
    RCC_AHB2ENR |= RCC_AHB2ENR_ADCEN;

    /* Common: ADC clock = HCLK (CKMODE=01), enable the temperature sensor
       and the internal voltage reference. */
    ADC_CCR |= ADC_CCR_CKMODE_HCLK1 | ADC_CCR_TSEN | ADC_CCR_VREFEN;

    /* Exit deep power-down, enable the regulator, let it settle. */
    ADC1_CR &= ~ADC_CR_DEEPPWD;
    ADC1_CR |= ADC_CR_ADVREGEN;
    adc_busy_wait(ADC_VREG_STARTUP_LOOPS);

    /* Single-ended calibration (ADCALDIF is 0 at reset). */
    ADC1_CR |= ADC_CR_ADCAL;
    while ((ADC1_CR & ADC_CR_ADCAL) != 0U) {
    }

    /* Long sample time for channel 17 (SMPR2 covers channels 10..18). */
    ADC1_SMPR2 |= (ADC_SAMPLE_640CYC
                   << ((ADC_TEMP_CHANNEL - 10U) * ADC_SMPR_FIELD_BITS));
    /* And for channel 0 (SMPR1 covers channels 0..9). */
    ADC1_SMPR1 |= (ADC_SAMPLE_640CYC
                   << (ADC_VREF_CHANNEL * ADC_SMPR_FIELD_BITS));

    /* Enable and wait until ready. */
    ADC1_CR |= ADC_CR_ADEN;
    while ((ADC1_ISR & ADC_ISR_ADRDY) == 0U) {
    }
    ADC1_ISR = ADC_ISR_ADRDY;  /* clear by writing 1 */
}

uint32_t adc_temp_read(void) {
    return adc_convert(ADC_TEMP_CHANNEL);
}

uint32_t adc_vref_read(void) {
    return adc_convert(ADC_VREF_CHANNEL);
}

uint16_t adc_ts_cal1(void) {
    return TS_CAL1_ADDR;
}

uint16_t adc_ts_cal2(void) {
    return TS_CAL2_ADDR;
}

uint16_t adc_vrefint_cal(void) {
    return VREFINT_CAL_ADDR;
}
