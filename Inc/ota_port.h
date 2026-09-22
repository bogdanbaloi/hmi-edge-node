#ifndef OTA_PORT_H
#define OTA_PORT_H

#include "ota_update.h"

/**
 * @file ota_port.h
 * @brief What ::ota_update_port_t is wired to on the Nucleo.
 *
 * Composition, like the temperature adapter in main.c: it joins the drivers
 * (`core`, `uart`, `flash`) to the update logic and touches no register
 * itself. It sits here, and not in a driver, because a driver that knew the
 * port type would make the HAL depend on application code.
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
#define OTA_PORT_FIRMWARE_VERSION 1U

/// The port, with every member set. Lives as long as the program. Needs
/// flash_init() to have run before the first INFO_REQ.
const ota_update_port_t *ota_port(void);

#endif /* OTA_PORT_H */
