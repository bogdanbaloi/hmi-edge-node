/**
 * @file oversize_image.c
 * @brief Not a test program: the proof that an image larger than one bank
 *        does not link.
 *
 * CI links the firmware once more with this file added and expects the link
 * to FAIL with the FLASH region overflowed. This array alone fills the whole
 * region the linker script allows, one bank minus its CONFIRMED page, so any
 * firmware code on top of it goes over. If that link ever succeeds, the limit
 * in STM32L476RGTX_FLASH.ld has been widened, and an image that could
 * overwrite the other bank or the CONFIRMED record would reach the board.
 *
 * Never part of the real build and not in tests/Makefile.
 */

#include <stdint.h>

/// The FLASH region in STM32L476RGTX_FLASH.ld: 512 KB minus one 2 KB page.
#define LINKER_FLASH_BYTES ((512U - 2U) * 1024U)

/// Kept even though nothing reads it, so the linker has to place it.
__attribute__((used)) const uint8_t k_oversize_image[LINKER_FLASH_BYTES] = { 1U };
