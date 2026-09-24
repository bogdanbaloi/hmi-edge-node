/**
 * @file option_plan.c
 * @brief What to write into the option bytes. See option_plan.h for the rules.
 */

#include "option_plan.h"

/// RDP occupies the low byte of FLASH_OPTR.
#define OPTION_RDP_MASK 0xFFUL
/// The only value that means "no read protection".
#define OPTION_RDP_LEVEL_0_VALUE 0xAAUL
/// The value that locks the chip forever.
#define OPTION_RDP_LEVEL_2_VALUE 0xCCUL
/// BFB2, bit 20: 1 boots from bank 2.
#define OPTION_BFB2 (1UL << 20)

option_rdp_t option_plan_rdp(uint32_t optr) {
    const uint32_t rdp = optr & OPTION_RDP_MASK;
    if (rdp == OPTION_RDP_LEVEL_0_VALUE) {
        return OPTION_RDP_LEVEL_0;
    }
    if (rdp == OPTION_RDP_LEVEL_2_VALUE) {
        return OPTION_RDP_LEVEL_2;
    }
    return OPTION_RDP_LEVEL_1;
}

uint32_t option_plan_boot_bank(uint32_t optr) {
    return ((optr & OPTION_BFB2) != 0U) ? OPTION_BANK_2 : OPTION_BANK_1;
}

option_plan_t option_plan_for_bank(uint32_t optr, uint32_t bank,
                                   uint32_t *out_optr) {
    if (bank != OPTION_BANK_1 && bank != OPTION_BANK_2) {
        return OPTION_PLAN_REFUSED_BANK;
    }
    if (option_plan_rdp(optr) != OPTION_RDP_LEVEL_0) {
        return OPTION_PLAN_REFUSED_RDP;
    }
    if (option_plan_boot_bank(optr) == bank) {
        return OPTION_PLAN_ALREADY;
    }
    /* One bit changes. Everything else is carried across as it was read. */
    *out_optr = (bank == OPTION_BANK_2) ? (optr | OPTION_BFB2)
                                        : (optr & ~OPTION_BFB2);
    return OPTION_PLAN_OK;
}
