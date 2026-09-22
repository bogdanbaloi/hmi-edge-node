#ifndef OTA_BOARD_H
#define OTA_BOARD_H

#include "ota_update.h"

/**
 * @file ota_board.h
 * @brief The board's side of the update port: what ::ota_update_port_t is
 *        wired to on the Nucleo.
 *
 * HAL, like `uart` and `adc`: the only place SYSCFG is touched, and later the
 * flash controller. The state machine never sees any of it.
 *
 * **What is real today (piece 3 of 7):** the clock, sending, and INFO. The
 * board answers INFO_REQ with its version and the bank it runs from.
 *
 * **What is refused, honestly, until its piece lands:** erasing and
 * programming the inactive bank (piece 5), confirming (piece 6), switching
 * banks and resetting (piece 7). Each returns a failure, so BEGIN is answered
 * NAK FLASH_ERROR. A board that claimed an update worked when nothing was
 * written would be worse than one that says it cannot yet.
 */

/**
 * The version this image reports in INFO. The spec leaves the meaning of the
 * u32 to the sender of BEGIN; this image is the first, so 1. An image built
 * to be sent as an update gets a higher one, which is how the host tells after
 * the switch that the new image really runs.
 */
#define OTA_BOARD_FIRMWARE_VERSION 1U

/// Turn on what the port needs. Call once, before the first frame can arrive.
void ota_board_init(void);

/// The port, with every member set. Lives as long as the program.
const ota_update_port_t *ota_board_port(void);

#endif /* OTA_BOARD_H */
