/**
 * @file ota_frame.c
 * @brief Envelope of the UART flash protocol: byte-by-byte parser and encoder.
 *
 * The resync rule is the part that shaped this file. On a false start the
 * parser must go back over bytes it has already taken in, because a real frame
 * can sit inside a false frame's body. That is why the parser keeps the raw
 * bytes and not just a position.
 *
 * The re-scan is a loop, not recursion. Recursing on every false start would
 * nest up to ~268 calls, each holding its own copy of the tail, which is tens
 * of kilobytes of stack on a microcontroller with 96 KB of RAM in total.
 * Instead the candidate is shifted forward in place and the same loop runs
 * again, so the stack use is fixed whatever arrives on the line.
 */

#include "ota_frame.h"

#include <string.h>

#include "crc16.h"

/* Offsets inside a frame. */
#define OFS_TYPE    1U
#define OFS_SEQ     2U
#define OFS_LEN     4U
#define OFS_PAYLOAD 6U

/// TYPE, SEQ and LEN: what the checksum covers besides the payload.
#define HEADER_LEN 5U

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static void write_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)(value >> 8);
}

/// Forgets the first n bytes of the candidate.
static void drop_front(ota_frame_parser_t *parser, uint16_t n) {
    if (n >= parser->count) {
        parser->count = 0U;
        return;
    }
    (void)memmove(parser->raw, &parser->raw[n], (size_t)(parser->count - n));
    parser->count = (uint16_t)(parser->count - n);
}

/// Moves the candidate to the next 0xA5 at or after index from, or empties it.
static void seek_start(ota_frame_parser_t *parser, uint16_t from) {
    for (uint16_t i = from; i < parser->count; i++) {
        if (parser->raw[i] == OTA_FRAME_START) {
            drop_front(parser, i);
            return;
        }
    }
    parser->count = 0U;
}

/// Checks one complete candidate and hands it to the sink.
static ota_frame_status_t deliver(const ota_frame_parser_t *parser,
                                  uint16_t len, ota_frame_sink_t sink,
                                  void *ctx) {
    const uint16_t sent = read_le16(&parser->raw[OFS_PAYLOAD + len]);
    const uint16_t computed =
        crc16_compute(&parser->raw[OFS_TYPE], (size_t)HEADER_LEN + len);
    ota_frame_t frame;
    frame.status = (computed == sent) ? OTA_FRAME_OK : OTA_FRAME_BAD_CRC;
    frame.type = parser->raw[OFS_TYPE];
    frame.seq = read_le16(&parser->raw[OFS_SEQ]);
    frame.len = len;
    frame.payload = &parser->raw[OFS_PAYLOAD];
    if (sink != NULL) {
        sink(&frame, ctx);
    }
    return frame.status;
}

/// Runs the candidate forward as far as the bytes already held allow.
static void process(ota_frame_parser_t *parser, ota_frame_sink_t sink,
                    void *ctx) {
    for (;;) {
        if (parser->count < OFS_PAYLOAD) {
            return;  /* LEN not known yet */
        }
        const uint16_t len = read_le16(&parser->raw[OFS_LEN]);
        if (len > OTA_FRAME_MAX_PAYLOAD) {
            /* False start. Resume from the byte after it, never skip LEN. */
            seek_start(parser, 1U);
            continue;
        }
        const uint16_t total = (uint16_t)(OTA_FRAME_OVERHEAD + len);
        if (parser->count < total) {
            return;  /* body still arriving */
        }
        if (deliver(parser, len, sink, ctx) == OTA_FRAME_OK) {
            drop_front(parser, total);
            seek_start(parser, 0U);  /* skip anything between two frames */
        } else {
            seek_start(parser, 1U);  /* same resync as a bad LEN */
        }
    }
}

void ota_frame_init(ota_frame_parser_t *parser) {
    parser->count = 0U;
}

void ota_frame_feed(ota_frame_parser_t *parser, uint8_t byte,
                    ota_frame_sink_t sink, void *ctx) {
    if (parser->count == 0U && byte != OTA_FRAME_START) {
        return;  /* idle: not a frame, drop it */
    }
    /* Unreachable by construction: after process() the candidate is always
       shorter than a complete frame, so it holds at most MAX_SIZE - 1 bytes.
       Kept so a future change cannot turn into a buffer overrun. */
    if (parser->count >= OTA_FRAME_MAX_SIZE) {
        parser->count = 0U;
        return;
    }
    parser->raw[parser->count] = byte;
    parser->count++;
    process(parser, sink, ctx);
}

size_t ota_frame_encode(const ota_frame_t *frame, uint8_t *out,
                        size_t out_cap) {
    if (frame == NULL || out == NULL) {
        return 0U;
    }
    const uint16_t len = frame->len;
    const size_t total = (size_t)OTA_FRAME_OVERHEAD + len;
    if (len > OTA_FRAME_MAX_PAYLOAD || out_cap < total ||
        (len > 0U && frame->payload == NULL)) {
        return 0U;
    }
    out[0] = OTA_FRAME_START;
    out[OFS_TYPE] = frame->type;
    write_le16(&out[OFS_SEQ], frame->seq);
    write_le16(&out[OFS_LEN], len);
    if (len > 0U) {
        (void)memcpy(&out[OFS_PAYLOAD], frame->payload, len);
    }
    write_le16(&out[OFS_PAYLOAD + len],
               crc16_compute(&out[OFS_TYPE], (size_t)HEADER_LEN + len));
    return total;
}
