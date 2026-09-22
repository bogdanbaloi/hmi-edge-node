#ifndef OTA_FRAME_H
#define OTA_FRAME_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file ota_frame.h
 * @brief Frames of the UART flash protocol: their layout, and building them.
 *
 * The protocol is a contract owned by both sides, written down in
 * industrial-hmi `docs/protocols/uart-flash-v1.md` (status AGREED,
 * 2026-09-22). Every message travels as one frame:
 *
 *     A5 | TYPE (1) | SEQ (2) | LEN (2) | PAYLOAD (0..260) | CRC16 (2)
 *
 * Multi-byte numbers are little-endian. The checksum is CRC-16/CCITT-FALSE
 * (see crc16.h) over every byte between 0xA5 and itself, so over TYPE, SEQ,
 * LEN and PAYLOAD, never over the start byte.
 *
 * This header is what a SENDER needs: the layout and the encoder. Receiving
 * lives in ota_frame_parser.h, so code that only transmits, like the ACK and
 * NAK path, never sees the parser's state or functions. That split is the
 * interface segregation principle applied to a C module: a client depends
 * only on what it uses.
 *
 * This module only knows the envelope. What a TYPE means, and what to answer,
 * belongs to the update state machine above it.
 */

#define OTA_FRAME_START       0xA5U  ///< First byte of every frame.
#define OTA_FRAME_MAX_PAYLOAD 260U   ///< DATA: a 4-byte offset plus 256 bytes.
#define OTA_FRAME_OVERHEAD    8U     ///< Start, TYPE, SEQ, LEN and CRC16.
/// Largest frame on the wire, and the size of the parser's buffer.
#define OTA_FRAME_MAX_SIZE    (OTA_FRAME_OVERHEAD + OTA_FRAME_MAX_PAYLOAD)
/// What ::ota_frame_encode returns when it built nothing. No frame is empty,
/// the smallest is ::OTA_FRAME_OVERHEAD bytes, so zero cannot be a real size.
#define OTA_FRAME_ENCODE_REFUSED 0U

/**
 * One message: what goes into a frame, and what comes out of one.
 *
 * Just the message. Whether a received frame's checksum held is the parser's
 * verdict about it, not part of the message, so it travels separately (see
 * ota_frame_parser.h). An earlier version kept it in here, which gave every
 * sender a field it had to ignore.
 */
typedef struct {
    uint8_t type;
    uint16_t seq;
    uint16_t len;
    /// For a received frame, points into the parser's buffer: valid only
    /// during the sink call.
    const uint8_t *payload;
} ota_frame_t;

/**
 * @brief Build one frame into out.
 *
 * Takes the same ::ota_frame_t the parser hands to its sink, so the API reads
 * the same in both directions and a received frame can be encoded again as is.
 *
 * It takes a struct and not six loose arguments on purpose: the static
 * analysis gate caps a function at five parameters, and four of these six are
 * the frame's own fields anyway.
 *
 * @return The frame's size in bytes, or ::OTA_FRAME_ENCODE_REFUSED when frame
 *         or out is missing, len is above ::OTA_FRAME_MAX_PAYLOAD, out is too
 *         small, or a payload is missing.
 */
size_t ota_frame_encode(const ota_frame_t *frame, uint8_t *out,
                        size_t out_cap);

#endif /* OTA_FRAME_H */
