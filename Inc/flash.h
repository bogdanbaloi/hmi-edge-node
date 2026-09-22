#ifndef FLASH_H
#define FLASH_H

/**
 * @file flash.h
 * @brief The flash banks of the STM32L476RG. HAL: the only place SYSCFG is
 *        touched, and from piece 5 on the flash controller too.
 *
 * Knows nothing about updates. The update port is built from it in the
 * composition layer (ota_port.c), the same way `adc` knows counts and never
 * degrees.
 */

/// One flash bank of the STM32L476RG: 512 KB (RM0351, dual bank mode).
#define FLASH_BANK_BYTES (512U * 1024U)
/// One flash page, the smallest erasable unit: 2 KB.
#define FLASH_PAGE_BYTES (2U * 1024U)

/// The two banks, numbered as ST numbers them in RM0351.
typedef enum {
    FLASH_BANK_1 = 1,
    FLASH_BANK_2 = 2
} flash_bank_t;

/// Turn on what reading the bank needs. Call once, at start-up.
void flash_init(void);

/**
 * @brief The bank the CPU runs from, the one mapped at 0x08000000.
 *
 * From SYSCFG_MEMRMP.FB_MODE, which the boot code sets from the BFB2 option
 * bit, so it is the bank really running, not the one that was meant to.
 */
flash_bank_t flash_running_bank(void);

#endif /* FLASH_H */
