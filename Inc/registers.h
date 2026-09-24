#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>

/*
 * Minimal STM32L476RG register map, ONLY the registers this firmware
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
/* An 8-bit view of a register, for peripherals where the ACCESS SIZE is part
 * of the meaning: writing one byte to the CRC data register feeds one byte,
 * writing four feeds four. */
#define REG8(addr) (*(volatile uint8_t *)(addr))

/* ---- RCC: Reset and Clock Control @ 0x40021000 ------------------------- */
/* Peripherals are clock-gated off at reset; we must enable each port/UART. */
#define RCC_AHB2ENR            REG32(0x4002104CUL) /* GPIO port clocks       */
#define RCC_APB1ENR1           REG32(0x40021058UL) /* USART2 clock (on APB1) */
#define RCC_APB2ENR            REG32(0x40021060UL) /* SYSCFG clock (on APB2) */
#define RCC_AHB2ENR_GPIOAEN    (1UL << 0)          /* enable GPIOA clock     */
#define RCC_AHB2ENR_GPIOCEN    (1UL << 2)          /* enable GPIOC clock     */
#define RCC_APB1ENR1_USART2EN  (1UL << 17)         /* enable USART2 clock    */
#define RCC_APB2ENR_SYSCFGEN   (1UL << 0)          /* enable SYSCFG clock    */

/* ---- GPIOA @ 0x48000000  (PA2/PA3 = USART2 TX/RX AF7, PA5 = LED LD2) --- */
#define GPIOA_MODER            REG32(0x48000000UL) /* pin mode, 2 bits/pin   */
#define GPIOA_ODR              REG32(0x48000014UL) /* output data            */
#define GPIOA_AFRL             REG32(0x48000020UL) /* alt function, pins 0-7 */

/* ---- GPIO fields, the same layout on every port (RM0351 GPIO chapter) ---
 * MODER and PUPDR give each pin 2 bits, AFRL gives pins 0 to 7 4 bits each.
 * Each macro places a value in one pin's field; the _MASK forms cover the
 * whole field, for clearing it first. */
#define GPIO_FIELD2_BITS       2U                  /* MODER, PUPDR           */
#define GPIO_FIELD4_BITS       4U                  /* AFRL                   */
#define GPIO_FIELD2_ALL        3U                  /* every bit of a 2-bit field */
#define GPIO_FIELD4_ALL        0xFU                /* every bit of a 4-bit field */
#define GPIO_MODE(pin, mode)   ((uint32_t)(mode) << (GPIO_FIELD2_BITS * (pin)))
#define GPIO_MODE_MASK(pin)    GPIO_MODE(pin, GPIO_FIELD2_ALL)
#define GPIO_PULL(pin, pull)   ((uint32_t)(pull) << (GPIO_FIELD2_BITS * (pin)))
#define GPIO_PULL_MASK(pin)    GPIO_PULL(pin, GPIO_FIELD2_ALL)
#define GPIO_AF(pin, af)       ((uint32_t)(af) << (GPIO_FIELD4_BITS * (pin)))
#define GPIO_AF_MASK(pin)      GPIO_AF(pin, GPIO_FIELD4_ALL)
#define GPIO_MODE_OUTPUT       1U                  /* general-purpose output */
#define GPIO_MODE_ALTERNATE    2U                  /* a peripheral drives it */
#define GPIO_PULL_UP           1U
#define GPIO_AF7_USART         7U  /* USART1-3 on AF7, DS10198 Table 17 */

/* ---- GPIOC @ 0x48000800  (PC13 = USER button B1) ---------------------- */
#define GPIOC_MODER            REG32(0x48000800UL) /* pin mode, 2 bits/pin   */
#define GPIOC_PUPDR            REG32(0x4800080CUL) /* pull-up/down, 2 bits   */
#define GPIOC_IDR              REG32(0x48000810UL) /* input data             */

/* ---- USART2 @ 0x40004400 --------------------------------------------- */
#define USART2_CR1             REG32(0x40004400UL) /* control register 1     */
#define USART2_BRR             REG32(0x4000440CUL) /* baud rate register     */
#define USART2_ISR             REG32(0x4000441CUL) /* status register        */
#define USART2_ICR             REG32(0x40004420UL) /* interrupt flag clear   */
#define USART2_RDR             REG32(0x40004424UL) /* receive data register  */
#define USART2_TDR             REG32(0x40004428UL) /* transmit data register */
#define USART2_CR1_UE          (1UL << 0)          /* USART enable           */
#define USART2_CR1_RE          (1UL << 2)          /* receiver enable        */
#define USART2_CR1_TE          (1UL << 3)          /* transmitter enable     */
#define USART2_CR1_RXNEIE      (1UL << 5)          /* interrupt on RX / ORE  */
#define USART2_ISR_PE          (1UL << 0)          /* parity error           */
#define USART2_ISR_FE          (1UL << 1)          /* framing error          */
#define USART2_ISR_NF          (1UL << 2)          /* noise detected         */
#define USART2_ISR_ORE         (1UL << 3)          /* overrun: a byte lost   */
#define USART2_ISR_RXNE        (1UL << 5)          /* a received byte waits  */
#define USART2_ISR_TXE         (1UL << 7)          /* TX data register empty */
/* ICR bits sit at the same positions as the ISR flags they clear. */
#define USART2_ICR_ERRORS      (USART2_ISR_PE | USART2_ISR_FE | USART2_ISR_NF | \
                                USART2_ISR_ORE)

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

/* ---- SysTick: the core's 24-bit down counter (Cortex-M, not STM32) -------
 * Counts down from RELOAD to zero at the core clock, then reloads and sets
 * COUNTFLAG. Polled here rather than using its interrupt: a millisecond tick
 * read from the main loop is enough. The one interrupt this firmware enables
 * is the UART receive, which cannot be polled, see uart.c.
 * Addresses from the ARMv7-M Architecture Reference Manual. */
#define SYST_CSR               REG32(0xE000E010UL) /* control and status      */
#define SYST_RVR               REG32(0xE000E014UL) /* reload value            */
#define SYST_CVR               REG32(0xE000E018UL) /* current value           */
#define SYST_CSR_ENABLE        (1UL << 0)          /* start counting          */
#define SYST_CSR_CLKSOURCE     (1UL << 2)          /* 1 = core clock          */
#define SYST_CSR_COUNTFLAG     (1UL << 16)         /* set when it hit zero    */
/* The counter is 24 bits, so it spans 16777215 ticks. Left free running at
 * that full range, which at the 4 MHz MSI reset clock is about 4.19 seconds
 * between wraps. Elapsed time is derived from the difference between two
 * readings, so any gap shorter than one wrap is counted exactly. */
#define SYST_COUNTER_MAX       0x00FFFFFFUL
/* Core clock is the 4 MHz MSI reset clock, so a millisecond is 4000 ticks.
 * Change the clock and this must follow, which is why it is written as the
 * arithmetic rather than as a bare number. */
#define SYST_TICKS_PER_MS      (4000000UL / 1000UL)

/* ---- SCB: System Control Block (Cortex-M4 core, not STM32 peripheral) ----
 * CPACR controls access to the coprocessors, and the FPU is coprocessors 10
 * and 11. It resets to "access denied", so the FPU is OFF after every reset
 * no matter what the compiler flags say. Enabling it is a RUNTIME step that
 * the build flags cannot do for you (see SystemInit in main.c).
 *
 * Addresses from the ARMv7-M Architecture Reference Manual, not RM0351: this
 * is core, and it is identical on every Cortex-M4. */
#define SCB_VTOR               REG32(0xE000ED08UL) /* vector table address    */
#define SCB_CPACR              REG32(0xE000ED88UL) /* coprocessor access      */
/* CP10 and CP11, full access, 0b11 each at bits [21:20] and [23:22]. */
#define SCB_CPACR_FPU_FULL     (0xFUL << 20)

/* ---- IWDG: independent watchdog @ 0x40003000 (RM0351 Rev 9, section 36) -
 * Clocked by the LSI, its own 32 kHz oscillator, so it keeps counting even if
 * the main clock dies. Started by writing a key; after that it CANNOT be
 * stopped, only fed. */
#define IWDG_KR                REG32(0x40003000UL) /* keys go here           */
#define IWDG_PR                REG32(0x40003004UL) /* prescaler              */
#define IWDG_RLR               REG32(0x40003008UL) /* reload value           */
#define IWDG_SR                REG32(0x4000300CUL) /* update in progress     */
#define IWDG_KEY_START         0x0000CCCCUL        /* start counting         */
#define IWDG_KEY_UNLOCK        0x00005555UL        /* allow PR and RLR       */
#define IWDG_KEY_FEED          0x0000AAAAUL        /* reload, stay alive     */
#define IWDG_PR_DIV256         6UL                 /* 110: LSI / 256         */
#define IWDG_RLR_MAX           0x0FFFUL            /* 12-bit counter         */
#define IWDG_SR_IDLE           0UL                 /* no update in progress  */

/* ---- RCC clock control and status @ 0x40021094 ------------------------
 * Says what caused the last reset, and clears those flags. */
#define RCC_CSR                REG32(0x40021094UL)
#define RCC_CSR_RMVF           (1UL << 23)         /* clear the reset flags  */
#define RCC_CSR_IWDGRSTF       (1UL << 29)         /* the watchdog reset us  */

/* ---- CRC calculation unit @ 0x40023000 (RM0351 Rev 9, section 15) ------
 * Its reset values are already the CRC-32 polynomial 0x04C11DB7 and the
 * initial value 0xFFFFFFFF, so only the bit reversals have to be set. The
 * final XOR is not in the hardware and is done in software. */
#define RCC_AHB1ENR            REG32(0x40021048UL) /* CRC unit clock         */
#define RCC_AHB1ENR_CRCEN      (1UL << 12)         /* enable the CRC clock   */
#define CRC_DR                 REG32(0x40023000UL) /* feed data here         */
#define CRC_DR_BYTE            REG8(0x40023000UL)  /* ... one byte at a time */
#define CRC_CR                 REG32(0x40023008UL) /* reversals and reset    */
#define CRC_CR_RESET           (1UL << 0)          /* back to the init value */
#define CRC_CR_REV_IN_BYTE     (1UL << 5)          /* 01: reverse each byte  */
#define CRC_CR_REV_IN_WORD     (3UL << 5)          /* 11: reverse each word  */
#define CRC_CR_REV_OUT         (1UL << 7)          /* reverse the result     */

/* ---- FLASH controller @ 0x40022000 (RM0351 Rev 9, section 3.7) ---------
 * Erasing and programming the other bank, plus, since piece 7, the option
 * bytes. **The option byte registers are the dangerous ones**: the same word
 * holds RDP, and RDP level 2 locks the chip forever. They were left undefined
 * on purpose until the piece that needs them, and the guard that replaces
 * that absence now lives in option_plan.c, which refuses anything but RDP
 * level 0, and in option_bytes.c, which refuses to write at all unless the
 * build says otherwise. */
#define FLASH_ACR              REG32(0x40022000UL) /* caches live here       */
#define FLASH_KEYR             REG32(0x40022008UL) /* unlock keys go here    */
#define FLASH_SR               REG32(0x40022010UL) /* status and errors      */
#define FLASH_CR               REG32(0x40022014UL) /* what to do, and START  */
/* The unlock sequence, section 3.3.5: these two values, in this order. A wrong
 * sequence locks FLASH_CR until the next reset and raises a Hard Fault. */
#define FLASH_KEY1             0x45670123UL
#define FLASH_KEY2             0xCDEF89ABUL
#define FLASH_CR_PG            (1UL << 0)          /* programming enabled    */
#define FLASH_CR_MER1          (1UL << 2)          /* mass erase bank 1      */
#define FLASH_CR_MER2          (1UL << 15)         /* mass erase bank 2      */
#define FLASH_CR_START         (1UL << 16)         /* begin the erase        */
#define FLASH_CR_LOCK          (1UL << 31)         /* lock FLASH_CR again    */
/* ---- the option bytes, section 3.4. Read freely, write almost never ----- */
#define FLASH_OPTKEYR          REG32(0x4002200CUL) /* unlocks the options    */
#define FLASH_OPTR             REG32(0x40022020UL) /* RDP, BFB2 and the rest */
#define FLASH_OPTKEY1          0x08192A3BUL        /* first option key       */
#define FLASH_OPTKEY2          0x4C5D6E7FUL        /* second option key      */
#define FLASH_CR_OPTSTRT       (1UL << 17)         /* write the options      */
#define FLASH_CR_OBL_LAUNCH    (1UL << 27)         /* load them: RESETS the chip */
#define FLASH_CR_OPTLOCK       (1UL << 30)         /* lock the options again */
#define FLASH_SR_EOP           (1UL << 0)          /* operation finished OK  */
#define FLASH_SR_BSY           (1UL << 16)         /* an operation is running */
#define FLASH_ACR_DCEN         (1UL << 10)         /* data cache enabled     */
#define FLASH_ACR_DCRST        (1UL << 12)         /* reset the data cache   */
/* Every error flag of FLASH_SR, section 3.7.5. Cleared by writing them back,
 * and they must be clear before an operation or PGSERR is set. */
#define FLASH_SR_ERRORS        ((1UL << 1) | (1UL << 3) | (1UL << 4) |  \
                                (1UL << 5) | (1UL << 6) | (1UL << 7) |  \
                                (1UL << 8) | (1UL << 9) | (1UL << 14) | \
                                (1UL << 15))

/* ---- SYSCFG @ 0x40010000 ----------------------------------------------
 * MEMRMP.FB_MODE says which flash bank is mapped at 0x08000000, the one the
 * CPU runs from: 0 = bank 1, 1 = bank 2. Set by the boot code from the BFB2
 * option bit, so it is how a running image learns which bank it is in. */
#define SYSCFG_MEMRMP          REG32(0x40010000UL) /* memory remap           */
#define SYSCFG_MEMRMP_FB_MODE  (1UL << 8)          /* 1 = bank 2 at 0x0800.. */

/* ---- NVIC: interrupt enables (Cortex-M4 core, ARMv7-M ARM) --------------
 * One bit per interrupt, 32 per register, so interrupt n is bit n % 32 of
 * ISER[n / 32]. Writing a 0 changes nothing, so no read-modify-write. */
#define NVIC_ISER(n)           REG32(0xE000E100UL + (4UL * ((n) / 32U)))
#define NVIC_ISER_BIT(n)       (1UL << ((n) % 32U))
/* Interrupt numbers, from the vector table in the startup file and ST's SVD. */
#define IRQ_USART2             38U

/* Pin numbers within their ports. */
#define PIN_USART2_TX  2U   /* PA2  */
#define PIN_USART2_RX  3U   /* PA3  */
#define PIN_LED        5U   /* PA5  */
#define PIN_BUTTON     13U  /* PC13 */

#endif /* REGISTERS_H */
