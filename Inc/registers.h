#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>

/*
 * Minimal STM32L476RG register map -- ONLY the registers this firmware
 * touches. Addresses and bit positions taken from the STM32L4 reference
 * manual (RM0351) and the datasheet memory map.
 *
 * Hand-defined on purpose: no CMSIS / no HAL. Every hardware access is an
 * explicit write to a named address, so the whole button-to-UART path is
 * visible and defensible line by line.
 */

/* A 32-bit memory-mapped register at a fixed address. `volatile` so the
 * compiler never caches or reorders these hardware accesses. */
#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- RCC: Reset and Clock Control @ 0x40021000 ------------------------- */
/* Peripherals are clock-gated off at reset; we must enable each port/UART. */
#define RCC_AHB2ENR            REG32(0x4002104CUL) /* GPIO port clocks       */
#define RCC_APB1ENR1           REG32(0x40021058UL) /* USART2 clock (on APB1) */
#define RCC_AHB2ENR_GPIOAEN    (1UL << 0)          /* enable GPIOA clock     */
#define RCC_AHB2ENR_GPIOCEN    (1UL << 2)          /* enable GPIOC clock     */
#define RCC_APB1ENR1_USART2EN  (1UL << 17)         /* enable USART2 clock    */

/* ---- GPIOA @ 0x48000000  (PA2 = USART2_TX AF7, PA5 = LED LD2) ---------- */
#define GPIOA_MODER            REG32(0x48000000UL) /* pin mode, 2 bits/pin   */
#define GPIOA_ODR              REG32(0x48000014UL) /* output data            */
#define GPIOA_AFRL             REG32(0x48000020UL) /* alt function, pins 0-7 */

/* ---- GPIOC @ 0x48000800  (PC13 = USER button B1) ---------------------- */
#define GPIOC_MODER            REG32(0x48000800UL) /* pin mode, 2 bits/pin   */
#define GPIOC_PUPDR            REG32(0x4800080CUL) /* pull-up/down, 2 bits   */
#define GPIOC_IDR              REG32(0x48000810UL) /* input data             */

/* ---- USART2 @ 0x40004400 --------------------------------------------- */
#define USART2_CR1             REG32(0x40004400UL) /* control register 1     */
#define USART2_BRR             REG32(0x4000440CUL) /* baud rate register     */
#define USART2_ISR             REG32(0x4000441CUL) /* status register        */
#define USART2_TDR             REG32(0x40004428UL) /* transmit data register */
#define USART2_CR1_UE          (1UL << 0)          /* USART enable           */
#define USART2_CR1_TE          (1UL << 3)          /* transmitter enable     */
#define USART2_ISR_TXE         (1UL << 7)          /* TX data register empty */

/* ---- ADC1 @ 0x50040000, common regs @ 0x50040300 (internal temp sensor) - */
#define ADC1_ISR               REG32(0x50040000UL) /* status: ADRDY, EOC     */
#define ADC1_CR                REG32(0x50040008UL) /* control: enable/cal/reg */
#define ADC1_SMPR1             REG32(0x50040014UL) /* sample time, ch 0-9     */
#define ADC1_SMPR2             REG32(0x50040018UL) /* sample time, ch 10-18   */
#define ADC1_SQR1              REG32(0x50040030UL) /* regular sequence        */
#define ADC1_DR                REG32(0x50040040UL) /* conversion result       */
#define ADC_CCR                REG32(0x50040308UL) /* common: clock + TSEN    */
#define RCC_AHB2ENR_ADCEN      (1UL << 13)         /* enable ADC clock        */
#define ADC_ISR_ADRDY          (1UL << 0)          /* ADC ready               */
#define ADC_ISR_EOC            (1UL << 2)          /* end of conversion       */
#define ADC_CR_ADEN            (1UL << 0)          /* enable ADC              */
#define ADC_CR_ADSTART         (1UL << 2)          /* start a conversion      */
#define ADC_CR_ADVREGEN        (1UL << 28)         /* ADC voltage regulator   */
#define ADC_CR_DEEPPWD         (1UL << 29)         /* deep power-down (reset) */
#define ADC_CR_ADCAL           (1UL << 31)         /* start calibration       */
#define ADC_CCR_CKMODE_HCLK1   (1UL << 16)         /* CKMODE=01: clock = HCLK */
#define ADC_CCR_VREFEN         (1UL << 22)         /* enable VREFINT channel  */
#define ADC_CCR_TSEN           (1UL << 23)         /* enable temperature sensor */
#define ADC_TEMP_CHANNEL       17U                 /* temp sensor = ADC1_IN17 */
#define ADC_VREF_CHANNEL       0U                  /* VREFINT   = ADC1_IN0    */

/* ---- Factory calibration, burned in system memory (RM0351 + datasheet) ---
 * All three were measured by ST at VDDA = 3.0 V. TS_CAL1 and VREFINT_CAL at
 * 30 C, TS_CAL2 at 130 C. A Nucleo runs VDDA at 3.3 V, so a raw temperature
 * reading MUST be rescaled by VREFINT before the calibration line applies --
 * see temperature.c. They are 16-bit, hence REG16. */
#define REG16(addr) (*(volatile uint16_t *)(addr))
#define TS_CAL1_ADDR           REG16(0x1FFF75A8UL) /* temp raw @ 30 C         */
#define VREFINT_CAL_ADDR       REG16(0x1FFF75AAUL) /* VREFINT raw @ 30 C      */
#define TS_CAL2_ADDR           REG16(0x1FFF75CAUL) /* temp raw @ 130 C        */

/* Pin numbers within their ports. */
#define PIN_USART2_TX  2U   /* PA2  */
#define PIN_LED        5U   /* PA5  */
#define PIN_BUTTON     13U  /* PC13 */

#endif /* REGISTERS_H */
