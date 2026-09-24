#ifndef ARMING_H
#define ARMING_H

/**
 * @file arming.h
 * @brief The one switch that lets this firmware write option bytes.
 *
 * Option bytes are the only irreversible thing on this chip: the same word
 * that holds the bank switch holds `RDP`, and `RDP` level 2 locks the chip
 * forever. Two guards decide WHETHER a write is allowed (option_plan.c) and
 * WHAT is written (option_bytes.c). This file is the third: whether the
 * firmware may write at all.
 *
 * It lives in its own file, with nothing else in it, so that arming is one
 * line in one diff, visible to anybody reading the history, rather than a
 * checkbox in an IDE that no reviewer ever sees.
 *
 * **Disarmed:** `option_bytes_boot_from()` touches no register and answers
 * `OPTION_WRITE_DISARMED`, so a COMMIT ends in `NAK FLASH_ERROR` after the
 * image has been written and verified. That is what the board did through
 * pieces 5 and 6.
 *
 * **Armed:** the board writes the option bytes, answers the ACK, and then
 * resets into the other bank. An update becomes real, and so does the
 * rollback of a trial image that hangs.
 *
 * Armed deliberately on 2026-09-24, with Bogdan at the board, after the
 * option bytes were read and `RDP` was confirmed to be level 0 (`0xAA`).
 */

#define OTA_BANK_SWITCH_ARMED 1

#endif /* ARMING_H */
