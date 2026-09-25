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
 * **What is real:** the whole chain. The clock, sending, INFO with the real
 * image state, erasing and programming the spare bank, verifying it, CONFIRM,
 * which writes the record that keeps the running image, selecting the new
 * boot bank through the option bytes, and the reset that applies it.
 *
 * This paragraph said "piece 6 of 7" and listed bank switching as refused
 * until 2026-09-25, which stopped being true when piece 7 landed. A header
 * describing a stage the code has left is read as current by whoever opens it
 * next.
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

/**
 * @brief Work out the running image's identity now, so INFO_REQ does not.
 *
 * INFO reports whether the running image is confirmed, which means checksumming
 * everything the linker put in flash. That value cannot change while this image
 * runs, so it is computed once here instead of on every request: about 13 ms at
 * the current size, about 0.7 s at the largest image the linker allows.
 *
 * Call after flash_init() and crc_unit_init(). **Skipping it cannot produce a
 * wrong answer**, only a slower first INFO_REQ, because the first caller works
 * the same value out for itself. See ota_port.c for why caching it is sound.
 */
void ota_port_init(void);

#endif /* OTA_PORT_H */
