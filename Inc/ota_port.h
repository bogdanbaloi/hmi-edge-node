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
 * **What is real today (piece 6 of 7):** the clock, sending, INFO with the
 * real image state, erasing and programming the spare bank, verifying it, and
 * CONFIRM, which writes the record that keeps the running image.
 *
 * **What is refused, honestly, until piece 7:** switching banks and the reset
 * that follows it. So a full session ends at COMMIT with NAK FLASH_ERROR,
 * after the image has been written and verified. A board that claimed an
 * update worked when it cannot yet switch would be worse than one that says
 * so.
 */

/**
 * The version this image reports in INFO. The spec leaves the meaning of the
 * u32 to the sender of BEGIN; this image is the first, so 1.
 *
 * **Overridable at build time**, with `-DOTA_PORT_FIRMWARE_VERSION=2`, which
 * is how an image meant to be sent as an update is built: after the switch,
 * INFO reporting 2 is how the host knows the new image really runs. The
 * CONFIRMED record does not depend on this number, it uses a checksum of the
 * image, for the reason written in confirm_record.h.
 */
#ifndef OTA_PORT_FIRMWARE_VERSION
#define OTA_PORT_FIRMWARE_VERSION 1U
#endif

/// The port, with every member set. Lives as long as the program. Needs
/// flash_init() to have run before the first INFO_REQ.
const ota_update_port_t *ota_port(void);

#endif /* OTA_PORT_H */
