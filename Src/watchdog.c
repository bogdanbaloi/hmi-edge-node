/**
 * @file watchdog.c
 * @brief The independent watchdog. Sequence from RM0351 Rev 9, section 36.3.
 */

#include "watchdog.h"
#include "registers.h"

void watchdog_start(void) {
    /* Section 36.3.2, window option disabled: start, unlock, prescaler,
       reload, wait for the registers to take, then feed once. The window
       register is left at its reset value, which disables the window: with a
       window, feeding too EARLY also resets the board. */
    IWDG_KR = IWDG_KEY_START;
    IWDG_KR = IWDG_KEY_UNLOCK;
    IWDG_PR = IWDG_PR_DIV256;
    IWDG_RLR = IWDG_RLR_MAX;
    while (IWDG_SR != IWDG_SR_IDLE) {
    }
    IWDG_KR = IWDG_KEY_FEED;
}

void watchdog_feed(void) {
    IWDG_KR = IWDG_KEY_FEED;
}

uint32_t watchdog_caused_last_reset(void) {
    const uint32_t from_watchdog = ((RCC_CSR & RCC_CSR_IWDGRSTF) != 0U) ? 1U
                                                                       : 0U;
    /* The flags survive a reset and would otherwise still be set after the
       next one, turning a power-on into "the watchdog did it". */
    RCC_CSR |= RCC_CSR_RMVF;
    return from_watchdog;
}
