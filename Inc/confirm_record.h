#ifndef CONFIRM_RECORD_H
#define CONFIRM_RECORD_H

#include <stdint.h>

/**
 * @file confirm_record.h
 * @brief The CONFIRMED record: what "this image is kept" looks like in flash.
 *
 * The protocol says an image runs on trial until the host sends CONFIRM, and
 * that **no record means unconfirmed** (uart-flash-v1.md, section 9, answer
 * 4). So the record is the only durable evidence, and a power cut can never
 * confirm an image by accident: an unwritten record reads as erased flash.
 *
 * It lives in the last 2 KB page of the running bank, which is why the linker
 * stops the image 2 KB before the end of the bank (piece 4), and why a mass
 * erase of the spare bank takes that bank's record with it: the image there
 * is being replaced, so its confirmation must go too.
 *
 * One double word, the smallest thing this flash can write:
 *
 *     bytes 0..3   the marker, ::CONFIRM_RECORD_MARKER
 *     bytes 4..7   the identity of the image that was confirmed
 *
 * The marker is checked, not just "is it not erased": erased flash reads as
 * all ones, but a half-written or stray value must not count as a
 * confirmation either.
 *
 * **The identity is the checksum of the running image, not its version.** A
 * version number is a constant in the source: every build carries the same
 * one until somebody remembers to raise it, and a record written for one
 * build then confirms any later build. That is not a theory, it happened
 * here on 2026-09-22: a record written for version 1, a different firmware
 * flashed over ST-Link, which erases only the pages the image occupies, and
 * the new image answered CONFIRMED for a confirmation it never received.
 * A checksum over the image changes whenever the image does.
 *
 * Pure logic, no register: flash.c writes what this builds, and the host test
 * checks the rules on a PC.
 */

/// Bytes of one record: one double word, the flash write granularity.
#define CONFIRM_RECORD_BYTES 8U

/// The marker, ASCII "CNFM" read as a little-endian word. A value with bits
/// both set and clear, so erased flash (all ones) and a bulk-zeroed page
/// (all zeros) are both rejected.
#define CONFIRM_RECORD_MARKER 0x4D464E43UL

/// Build one record for this image, ready to be programmed.
void confirm_record_build(uint8_t out[CONFIRM_RECORD_BYTES], uint32_t identity);

/**
 * @brief Is this a confirmation of that exact image?
 *
 * @return 1 when the marker matches AND the identity matches, 0 otherwise:
 *         erased flash, a stray value, or a record left by another image.
 */
uint32_t confirm_record_confirms(const uint8_t raw[CONFIRM_RECORD_BYTES],
                                 uint32_t identity);

#endif /* CONFIRM_RECORD_H */
