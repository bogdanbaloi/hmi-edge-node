/**
 * @file flash.c
 * @brief The flash banks. The only place SYSCFG registers are touched.
 */

#include "flash.h"
#include "registers.h"

void flash_init(void) {
    /* SYSCFG is clock-gated at reset, and a gated peripheral reads as 0,
       which would report bank 1 from either bank. */
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
}

flash_bank_t flash_running_bank(void) {
    return ((SYSCFG_MEMRMP & SYSCFG_MEMRMP_FB_MODE) != 0U) ? FLASH_BANK_2
                                                          : FLASH_BANK_1;
}
