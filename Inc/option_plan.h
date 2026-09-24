#ifndef OPTION_PLAN_H
#define OPTION_PLAN_H

#include <stdint.h>

/**
 * @file option_plan.h
 * @brief Deciding what to write into the option bytes, with no register in
 *        sight, so the decision can be tested on a PC.
 *
 * The option bytes are the one irreversible thing on this chip. `RDP` level 2
 * (`0xCC`) locks it forever: no ST-Link, no recovery, no second chance. The
 * bank switch this firmware needs is a single bit in the same word, `BFB2`.
 *
 * So the decision is separated from the writing. This module answers "what
 * word would we write, and are we allowed to write it at all", and it is the
 * part the host test can hammer. option_bytes.c only carries out a plan this
 * module approved.
 *
 * Every rule here is from RM0351 Rev 9, section 3.4 and the FLASH_OPTR
 * description: `RDP` is bits 7:0, `0xAA` is level 0, `0xCC` is level 2 and
 * anything else is level 1; `BFB2` is bit 20, 1 meaning boot from bank 2.
 */

/// Read protection, the reason this module exists.
typedef enum {
    OPTION_RDP_LEVEL_0 = 0,  ///< 0xAA: no read protection, the only safe one.
    OPTION_RDP_LEVEL_1,      ///< anything else: memories protected.
    OPTION_RDP_LEVEL_2       ///< 0xCC: chip locked forever, irreversible.
} option_rdp_t;

/// What came of asking for a bank switch.
typedef enum {
    OPTION_PLAN_OK = 0,       ///< A word to write, in out_optr.
    OPTION_PLAN_ALREADY,      ///< That bank already boots. Nothing to write.
    OPTION_PLAN_REFUSED_RDP,  ///< Not at RDP level 0. Nothing is written.
    OPTION_PLAN_REFUSED_BANK  ///< The bank asked for is not 1 or 2.
} option_plan_t;

/// The banks, as the protocol and RM0351 number them.
#define OPTION_BANK_1 1U
#define OPTION_BANK_2 2U

/// Read protection level from a FLASH_OPTR value.
option_rdp_t option_plan_rdp(uint32_t optr);

/// Which bank this OPTR boots: ::OPTION_BANK_1 or ::OPTION_BANK_2.
uint32_t option_plan_boot_bank(uint32_t optr);

/**
 * @brief The OPTR to write so the board boots from bank, if it is allowed.
 *
 * Changes exactly one bit, `BFB2`, and carries every other bit of the current
 * value across untouched. That matters more than it looks: the same word
 * holds `RDP`, the watchdog options and the brown-out level, and a plan that
 * rebuilt the word from scratch could quietly change any of them.
 *
 * Refuses unless the chip is at `RDP` level 0. At level 1 a write is the
 * wrong move; at level 2 there is nothing to be done at all, and a firmware
 * that tries anyway is one bug away from writing `0xCC` somewhere it should
 * not.
 *
 * @param optr      what FLASH_OPTR reads now.
 * @param bank      ::OPTION_BANK_1 or ::OPTION_BANK_2.
 * @param out_optr  set only when the answer is ::OPTION_PLAN_OK.
 */
option_plan_t option_plan_for_bank(uint32_t optr, uint32_t bank,
                                   uint32_t *out_optr);

#endif /* OPTION_PLAN_H */
