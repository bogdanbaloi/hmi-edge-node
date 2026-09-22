#ifndef CRC_UNIT_H
#define CRC_UNIT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file crc_unit.h
 * @brief The L4's CRC peripheral, set up for CRC-32/ISO-HDLC. HAL: the only
 *        place the CRC registers are touched.
 *
 * Same variant and same answer as crc32.h, which stays as the software
 * reference the host test can check against the catalogue. This exists for
 * one reason, and it is measured, not assumed: verifying a full 510 KB image
 * in software took about 4.6 s on the board, and the host allows 2 s for an
 * answer to COMMIT.
 *
 * The hardware already resets to the right polynomial (0x04C11DB7) and the
 * right initial value (0xFFFFFFFF). Only the bit reversals have to be set,
 * and the final XOR is done in software, because the peripheral has none
 * (RM0351 Rev 9, section 15).
 */

/// Turn the unit on and configure it. Call once, before crc_unit_compute().
void crc_unit_init(void);

/**
 * @brief CRC-32/ISO-HDLC over len bytes, computed by the peripheral.
 *
 * Feeds whole words while it can and the remaining bytes one at a time, which
 * is four times fewer bus writes for an image. Both paths are exercised by
 * crc_unit_agrees_with_software(), whose check string is nine bytes: two
 * words and one byte.
 */
uint32_t crc_unit_compute(const uint8_t *bytes, size_t len);

/**
 * @brief Does the peripheral produce the catalogue value?
 *
 * Checked once at start-up over the standard check string "123456789", whose
 * CRC-32/ISO-HDLC is 0xCBF43926. A misconfigured unit would silently make
 * every COMMIT fail with VERIFY_FAILED on a perfectly good image, so the
 * caller uses the software implementation instead when this returns 0.
 *
 * @return 1 when the unit can be trusted, 0 when it cannot.
 */
uint32_t crc_unit_is_trustworthy(void);

#endif /* CRC_UNIT_H */
