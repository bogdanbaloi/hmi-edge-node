/**
 * @file option_bytes.c
 * @brief The option bytes. Sequence from RM0351 Rev 9, section 3.4.
 *
 * Read what you like. Writing is what the three guards in option_bytes.h are
 * about, and the third one is right below.
 */

#include "option_bytes.h"
#include "registers.h"

uint32_t option_bytes_raw(void) {
    return FLASH_OPTR;
}

option_rdp_t option_bytes_rdp(void) {
    return option_plan_rdp(FLASH_OPTR);
}

uint32_t option_bytes_boot_bank(void) {
    return option_plan_boot_bank(FLASH_OPTR);
}

#ifdef OTA_BANK_SWITCH_ARMED

static void wait_while_busy(void) {
    while ((FLASH_SR & FLASH_SR_BSY) != 0U) {
    }
}

/// Section 3.4: FLASH_CR has to be unlocked first, then the options on top of
/// it, each with its own pair of keys and its own wrong-sequence penalty.
static void unlock_options(void) {
    if ((FLASH_CR & FLASH_CR_LOCK) != 0U) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    if ((FLASH_CR & FLASH_CR_OPTLOCK) != 0U) {
        FLASH_OPTKEYR = FLASH_OPTKEY1;
        FLASH_OPTKEYR = FLASH_OPTKEY2;
    }
}

/// Carries out an approved plan: write the word and start the write. It does
/// NOT load the options, because loading them resets the board and the host
/// is still waiting for an answer.
static option_write_t write_options(uint32_t planned) {
    wait_while_busy();
    unlock_options();
    FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;

    FLASH_OPTR = planned;
    FLASH_CR |= FLASH_CR_OPTSTRT;
    wait_while_busy();

    const option_write_t status = ((FLASH_SR & FLASH_SR_ERRORS) != 0U)
                                      ? OPTION_WRITE_FAILED
                                      : OPTION_WRITE_OK;
    FLASH_CR |= FLASH_CR_OPTLOCK;
    return status;
}

#endif /* OTA_BANK_SWITCH_ARMED */

void option_bytes_launch(void) {
#ifdef OTA_BANK_SWITCH_ARMED
    FLASH_CR |= FLASH_CR_OBL_LAUNCH;  /* resets the chip: nothing after this */
    for (;;) {
        /* Not reached. If the reset somehow does not happen, the watchdog
           ends it within about 32 s rather than leaving a board that wrote
           its options and then carried on as if it had not. */
    }
#endif
}

option_write_t option_bytes_boot_from(uint32_t bank) {
    uint32_t planned = 0U;
    const option_plan_t plan = option_plan_for_bank(FLASH_OPTR, bank, &planned);
    if (plan == OPTION_PLAN_ALREADY) {
        return OPTION_WRITE_ALREADY;
    }
    if (plan != OPTION_PLAN_OK) {
        return OPTION_WRITE_REFUSED;
    }

#ifdef OTA_BANK_SWITCH_ARMED
    return write_options(planned);
#else
    /* The plan is good and this build still will not write it. Arming is a
       deliberate act, made once, with somebody watching the board. */
    (void)planned;
    return OPTION_WRITE_DISARMED;
#endif
}
