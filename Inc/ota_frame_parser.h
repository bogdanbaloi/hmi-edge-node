#ifndef OTA_FRAME_PARSER_H
#define OTA_FRAME_PARSER_H

#include <stdint.h>

#include "ota_frame.h"

/**
 * @file ota_frame_parser.h
 * @brief Receiving frames of the UART flash protocol, one byte at a time.
 *
 * What a RECEIVER needs. The layout and the encoder are in ota_frame.h, which
 * this header includes. Code that only transmits includes ota_frame.h alone
 * and never sees anything declared here.
 *
 * Pure logic, no registers, no hardware. The parser takes one byte at a time,
 * which is how a UART delivers them, and hands every finished frame to an
 * injected sink, so a host test drives it with a capturing sink.
 */

/// The parser's verdict on a finished frame.
typedef enum {
    OTA_FRAME_OK = 0,   ///< The checksum matches.
    OTA_FRAME_BAD_CRC   ///< Complete, but the checksum does not match.
} ota_frame_status_t;

/**
 * Receives every finished frame, good or bad, with the verdict beside it. The
 * update state machine decides what to do with a bad one (the protocol answers
 * it with NAK BAD_CRC). A sink must not feed the same parser: it is called
 * from inside a feed.
 */
typedef void (*ota_frame_sink_t)(const ota_frame_t *frame,
                                 ota_frame_status_t status, void *ctx);

/**
 * Parser state. It keeps the raw bytes of the frame being built, not just a
 * position, because the resync rule needs them back (see
 * ::ota_frame_parser_feed). Visible here so a parser can be allocated
 * statically: there is no heap on the board.
 */
typedef struct {
    uint8_t raw[OTA_FRAME_MAX_SIZE];
    uint16_t count;  ///< Bytes of the current candidate held in raw.
} ota_frame_parser_t;

/// Empty the parser, as if nothing had arrived yet.
void ota_frame_parser_init(ota_frame_parser_t *parser);

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
void ota_frame_parser_feed(ota_frame_parser_t *parser, uint8_t byte,
                           ota_frame_sink_t sink, void *ctx);

#endif /* OTA_FRAME_PARSER_H */
