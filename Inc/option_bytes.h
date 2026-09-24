#ifndef OPTION_BYTES_H
#define OPTION_BYTES_H

#include "option_plan.h"

#include <stdint.h>

/**
 * @file option_bytes.h
 * @brief The option bytes on the board. HAL: the only place FLASH_OPTR and
 *        FLASH_OPTKEYR are touched.
 *
 * **This is the only irreversible thing this firmware can do.** The option
 * bytes hold `RDP`, and `RDP` level 2 locks the chip forever: no ST-Link, no
 * recovery, no way back. The bank switch the update chain needs is one bit in
 * the same word.
 *
 * Three guards, and they are deliberately not the same guard:
 *
 *  1. option_plan.c refuses any plan unless the chip reads `RDP` level 0, and
 *     changes exactly one bit of the word it was given. Tested on a PC, 98
 *     checks and 8 mutants.
 *  2. This module carries out a plan and nothing else. It cannot be asked to
 *     write an arbitrary word.
 *  3. **The write is disarmed at build time.** Without `OTA_BANK_SWITCH_ARMED`
 *     defined, option_bytes_boot_from() touches no register and answers
 *     ::OPTION_WRITE_DISARMED. Arming it is a one-line change to the build,
 *     made deliberately, with Bogdan at the board.
 *
 * Reading is always allowed and is how the guards are checked before anything
 * is armed.
 */

/// How a request to change the boot bank ended.
typedef enum {
    OPTION_WRITE_OK = 0,       ///< Written, and waiting to be loaded.
    OPTION_WRITE_ALREADY,      ///< That bank already boots. Nothing written.
    OPTION_WRITE_REFUSED,      ///< The plan refused it: RDP, or a bad bank.
    OPTION_WRITE_DISARMED,     ///< This build cannot write option bytes.
    OPTION_WRITE_FAILED        ///< The controller reported an error.
} option_write_t;

/// FLASH_OPTR exactly as it reads now. Always safe.
uint32_t option_bytes_raw(void);

/// Read protection level of this chip. Always safe.
option_rdp_t option_bytes_rdp(void);

/// The bank this chip boots from: ::OPTION_BANK_1 or ::OPTION_BANK_2.
uint32_t option_bytes_boot_bank(void);

/**
 * @brief Write the option bytes so the board will boot from bank.
 *
 * Writing and applying are two steps on purpose. RM0351 section 3.4: the new
 * options sit in flash until they are LOADED, and loading them resets the
 * chip. The protocol says COMMIT is answered with an ACK and only then does
 * the board reset, so this call must not reset anything: it writes, and
 * returns.
 */
option_write_t option_bytes_boot_from(uint32_t bank);

/**
 * @brief Apply the written options, which resets the board.
 *
 * Never returns when it works: setting `OBL_LAUNCH` generates a reset. Call
 * it after the answer the host is waiting for has gone out on the wire.
 */
void option_bytes_launch(void);

#endif /* OPTION_BYTES_H */
