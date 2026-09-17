/**
 * @file adc.c
 * @brief Internal temperature-sensor ADC driver. The L476 ADC resets in deep
 *        power-down, so the bring-up order matters (see adc_temp_init).
 */

#include "adc.h"
#include "registers.h"

/// SMPR field 111 = 640.5 cycles. The temperature sensor is high-impedance
/// and needs a long sample time or the reading is wrong.
#define ADC_TEMP_SAMPLE_640CYC 7UL
/// > 20us regulator start-up at 4 MHz, as a simple nop-loop count.
#define ADC_VREG_STARTUP_LOOPS 4000UL

static void adc_busy_wait(volatile uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

void adc_temp_init(void) {
    RCC_AHB2ENR |= RCC_AHB2ENR_ADCEN;

    /* Common: ADC clock = HCLK (CKMODE=01), enable the temperature sensor. */
    ADC_CCR |= ADC_CCR_CKMODE_HCLK1 | ADC_CCR_TSEN;

    /* Exit deep power-down, enable the regulator, let it settle. */
    ADC1_CR &= ~ADC_CR_DEEPPWD;
    ADC1_CR |= ADC_CR_ADVREGEN;
    adc_busy_wait(ADC_VREG_STARTUP_LOOPS);

    /* Single-ended calibration (ADCALDIF is 0 at reset). */
    ADC1_CR |= ADC_CR_ADCAL;
    while ((ADC1_CR & ADC_CR_ADCAL) != 0U) {
    }

    /* Long sample time for channel 17 (SMPR2 bits [23:21]). */
    ADC1_SMPR2 |= (ADC_TEMP_SAMPLE_640CYC << ((ADC_TEMP_CHANNEL - 10U) * 3U));

    /* One conversion of channel 17: L=0, SQ1=17 (bits [10:6]). */
    ADC1_SQR1 = (ADC_TEMP_CHANNEL << 6U);

    /* Enable and wait until ready. */
    ADC1_CR |= ADC_CR_ADEN;
    while ((ADC1_ISR & ADC_ISR_ADRDY) == 0U) {
    }
    ADC1_ISR = ADC_ISR_ADRDY;  /* clear by writing 1 */
}

uint32_t adc_temp_read(void) {
    ADC1_CR |= ADC_CR_ADSTART;
    while ((ADC1_ISR & ADC_ISR_EOC) == 0U) {
    }
    return ADC1_DR;
}
