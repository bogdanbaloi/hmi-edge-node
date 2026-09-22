#ifndef OTA_FRAME_H
#define OTA_FRAME_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file ota_frame.h
 * @brief Frames of the UART flash protocol: parsing and building them.
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
 * This module only knows the envelope. What a TYPE means, and what to answer,
 * belongs to the update state machine above it.
 *
 * Pure logic, no registers, no hardware. The parser takes one byte at a time,
 * which is how a UART delivers them, and hands every finished frame to an
 * injected sink, so a host test drives it with a capturing sink.
 */

#define OTA_FRAME_START       0xA5U  ///< First byte of every frame.
#define OTA_FRAME_MAX_PAYLOAD 260U   ///< DATA: a 4-byte offset plus 256 bytes.
#define OTA_FRAME_OVERHEAD    8U     ///< Start, TYPE, SEQ, LEN and CRC16.
/// Largest frame on the wire, and the size of the parser's buffer.
#define OTA_FRAME_MAX_SIZE    (OTA_FRAME_OVERHEAD + OTA_FRAME_MAX_PAYLOAD)

/// How a finished frame turned out.
typedef enum {
    OTA_FRAME_OK = 0,   ///< The checksum matches.
    OTA_FRAME_BAD_CRC   ///< Complete, but the checksum does not match.
} ota_frame_status_t;

/// One finished frame, as handed to the sink.
typedef struct {
    ota_frame_status_t status;
    uint8_t type;
    uint16_t seq;
    uint16_t len;
    /// Points into the parser's own buffer: valid only during the sink call.
    const uint8_t *payload;
} ota_frame_t;

/**
 * Receives every finished frame, good or bad. The update state machine decides
 * what to do with a bad one (the protocol answers it with NAK BAD_CRC).
 * A sink must not feed the same parser: it is called from inside a feed.
 */
typedef void (*ota_frame_sink_t)(const ota_frame_t *frame, void *ctx);

/**
 * Parser state. It keeps the raw bytes of the frame being built, not just a
 * position, because the resync rule needs them back (see ::ota_frame_feed).
 */
typedef struct {
    uint8_t raw[OTA_FRAME_MAX_SIZE];
    uint16_t count;  ///< Bytes of the current candidate held in raw.
} ota_frame_parser_t;

/// Empty the parser, as if nothing had arrived yet.
void ota_frame_init(ota_frame_parser_t *parser);

/**
 * @brief Feed one received byte.
 *
 * Bytes before a 0xA5 are dropped, which is how telemetry text, or anything
 * else on the line, is skipped. From a 0xA5 on, the bytes are kept until the
 * frame is complete, then the checksum is checked and the sink is called.
 *
 * **The resync rule (section 9, item 8, shared with the host).** A 0xA5 may be
 * a false start, a random byte that happens to have that value. It shows as a
 * LEN above 260 or a checksum that does not match. Either way the receiver
 * resumes looking for 0xA5 from the byte AFTER the false start, re-scanning
 * bytes it has already received. It never skips LEN bytes, because a garbage
 * LEN can reach 65535 and would swallow the real frames behind it. A real
 * frame hidden inside a false frame's body is therefore still found.
 *
 * One call can produce more than one frame, because a re-scan can uncover
 * several, so frames go to the sink instead of a return value.
 */
void ota_frame_feed(ota_frame_parser_t *parser, uint8_t byte,
                    ota_frame_sink_t sink, void *ctx);

/**
 * @brief Build one frame into out.
 *
 * Takes the same ::ota_frame_t the parser hands to its sink, so the API reads
 * the same in both directions and a parsed frame can be encoded again as is.
 * Its status is an output of the parser and is ignored here: type, seq, len
 * and payload are what go on the wire.
 *
 * It takes a struct and not six loose arguments on purpose: the static
 * analysis gate caps a function at five parameters, and four of these six are
 * the frame's own fields anyway.
 *
 * @return The frame's size in bytes, or 0 when frame or out is missing, len
 *         is above ::OTA_FRAME_MAX_PAYLOAD, out is too small, or a payload is
 *         missing.
 */
size_t ota_frame_encode(const ota_frame_t *frame, uint8_t *out,
                        size_t out_cap);

#endif /* OTA_FRAME_H */
