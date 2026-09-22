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

/// CRC-32/ISO-HDLC over len bytes, one byte per write.
uint32_t crc_unit_compute(const uint8_t *bytes, size_t len);

/**
 * @brief The same, for memory that is already word aligned: four bytes per
 *        write, and the last one to three bytes taken out of the next word.
 *
 * This is what an image in flash goes through, and it is the reason the bank
 * is offered as words by flash.h: the alignment belongs to the driver that
 * knows the address, not to a cast here. Measured on the board, a 64 KB image
 * takes 91 ms this way against 245 ms when each word is built from bytes.
 */
uint32_t crc_unit_compute_words(const uint32_t *words, size_t len);

/**
 * @brief Does the peripheral produce the catalogue value?
 *
 * Checked once at start-up over the standard check string "123456789", whose
 * CRC-32/ISO-HDLC is 0xCBF43926, ON BOTH PATHS: as bytes and as words, since
 * they are fed to the peripheral differently. A misconfigured unit would
 * silently make every COMMIT fail with VERIFY_FAILED on a perfectly good
 * image, so the caller uses the software implementation when this returns 0.
 *
 * @return 1 when the unit can be trusted, 0 when it cannot.
 */
uint32_t crc_unit_is_trustworthy(void);

#endif /* CRC_UNIT_H */
