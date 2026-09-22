#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file crc32.h
 * @brief CRC-32/ISO-HDLC over the received image, the variant the protocol
 *        pins (industrial-hmi uart-flash-v1.md section 4, AGREED).
 *
 * Reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, input and output
 * reflected, final XOR 0xFFFFFFFF. Its standard check value over the ASCII
 * string "123456789" is 0xCBF43926, and the test asserts exactly that: it is
 * the only proof that both sides compute the same CRC and not merely a CRC.
 * This is the same reasoning as crc16.h, one variant lower down the stack.
 *
 * Pure logic, no register: the host test drives it on a PC. It reads flash
 * only because the caller hands it a pointer into flash.
 *
 * **Speed, and why the table exists.** The obvious bitwise form costs about
 * 25 cycles per byte, so a full 510 KB image would take over 3 s at the 4 MHz
 * reset clock, and the host gives an answer 2 s. A 256-entry table brings it
 * to roughly a third of that. The table is built once into RAM rather than
 * stored in flash, which trades 1 KB of RAM for 1 KB of flash and keeps the
 * numbers in one place: the polynomial is written once, here.
 */

/// Build the lookup table. Call once, before the first crc32_compute().
void crc32_init(void);

/// CRC-32/ISO-HDLC over len bytes.
uint32_t crc32_compute(const uint8_t *bytes, size_t len);

#endif /* CRC32_H */
