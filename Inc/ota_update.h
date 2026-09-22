#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "ota_frame.h"
#include "ota_frame_parser.h"

/**
 * @file ota_update.h
 * @brief The update state machine: what the board answers to each message.
 *
 * The frame parser (ota_frame_parser.h) turns bytes into messages. This module
 * decides what each message means right now: is it allowed, what does the
 * board do, and what does it answer. Every rule comes from industrial-hmi
 * `docs/protocols/uart-flash-v1.md`, sections 4 to 6, status AGREED.
 *
 * Pure logic, like telemetry. Everything that only exists on the board is
 * injected through ::ota_update_port_t: the flash, the bank, the clock, the
 * UART. On the board those are the real drivers; in the host test they are a
 * RAM array, a counter and a capture buffer, so every rule is tested on a PC
 * before it reaches the board.
 *
 * It plugs straight into the parser: ::ota_update_on_frame has the exact shape
 * of ::ota_frame_sink_t, so the parser hands frames to it with no adapter.
 */

/* ---- message types, section 4 ------------------------------------------- */

#define OTA_MSG_INFO_REQ 0x01U  ///< Host: what are you running?
#define OTA_MSG_BEGIN    0x02U  ///< Host: a new image is coming.
#define OTA_MSG_DATA     0x03U  ///< Host: write these bytes at this offset.
#define OTA_MSG_COMMIT   0x04U  ///< Host: all sent, verify and switch banks.
#define OTA_MSG_CONFIRM  0x05U  ///< Host: the new image works, keep it.
#define OTA_MSG_ABORT    0x06U  ///< Host: cancel, leave the running image.
#define OTA_MSG_INFO     0x81U  ///< Board: the answer to INFO_REQ.
#define OTA_MSG_ACK      0x82U  ///< Board: done, SEQ says which frame.
#define OTA_MSG_NAK      0x83U  ///< Board: refused, the code says why.

/* ---- NAK codes, section 4 ----------------------------------------------- */

#define OTA_NAK_BAD_CRC       0x01U  ///< The frame checksum did not match.
#define OTA_NAK_BAD_STATE     0x02U  ///< Not allowed right now.
#define OTA_NAK_TOO_LARGE     0x03U  ///< The image does not fit.
#define OTA_NAK_BAD_OFFSET    0x04U  ///< A gap, an overlap, or outside the image.
#define OTA_NAK_FLASH_ERROR   0x05U  ///< Erasing or programming failed.
#define OTA_NAK_VERIFY_FAILED 0x06U  ///< CRC32 at COMMIT does not match BEGIN.
/**
 * Added to the spec on 2026-09-22 at firmware's request (AGREED): the message
 * is malformed, an unknown TYPE, or a payload whose length or value cannot
 * belong to it. The spec has no code for that, and borrowing BAD_STATE or
 * BAD_OFFSET would tell the host something false. The host treats it as a
 * bug to report, never as a reason to resend.
 */
#define OTA_NAK_BAD_MESSAGE   0x07U

/* ---- payload layouts, section 4 ----------------------------------------- */

#define OTA_BEGIN_LEN        12U  ///< size u32, image CRC32 u32, version u32.
#define OTA_BEGIN_SIZE_AT    0U   ///< BEGIN: the image size.
#define OTA_BEGIN_CRC32_AT   4U   ///< BEGIN: the image CRC32.
#define OTA_BEGIN_VERSION_AT 8U   ///< BEGIN: the new image's version.

#define OTA_INFO_LEN         6U   ///< version u32, active bank u8, state u8.
#define OTA_INFO_VERSION_AT  0U
#define OTA_INFO_BANK_AT     4U
#define OTA_INFO_STATE_AT    5U

#define OTA_DATA_OFFSET_AT   0U   ///< DATA: the u32 offset in the image.
#define OTA_DATA_BYTES_AT    4U   ///< DATA: image bytes start here.

#define OTA_NAK_LEN          1U   ///< One error code.
#define OTA_NAK_CODE_AT      0U
/// Image bytes per DATA frame come in multiples of this: the L4 programs
/// flash 8 bytes at a time.
#define OTA_DATA_ALIGN    8U

#define OTA_IMAGE_CONFIRMED 0U  ///< INFO state: the running image is kept.
#define OTA_IMAGE_TRIAL     1U  ///< INFO state: on trial, awaiting CONFIRM.

/**
 * How long a session may stay silent before the board gives up and returns to
 * normal mode, counted from the board's LAST ANSWER, not from the last frame
 * received. AGREED with industrial-hmi on 2026-09-22 and pinned in the
 * spec's "Timeouts" table, section 6: at BEGIN the board erases a whole bank
 * before it answers, and that is the board's own work, which must not count
 * against the host.
 */
#define OTA_UPDATE_SILENCE_MS 10000U

/// The result of a flash operation reported by the port.
typedef enum {
    OTA_IO_OK = 0,
    OTA_IO_FAILED
} ota_io_t;

/// What INFO reports about the image that is running now.
typedef struct {
    uint32_t version;
    uint8_t active_bank;
    uint8_t image_state;  ///< ::OTA_IMAGE_CONFIRMED or ::OTA_IMAGE_TRIAL
} ota_running_t;

/**
 * Everything the state machine needs from the board, as function pointers.
 * Every function gets ctx back, so a test can point it at its own fake.
 */
typedef struct {
    void *ctx;
    /// Bytes the inactive bank can take. Less than the bank itself: its last
    /// page is kept for the CONFIRMED record (answer 3 to industrial-hmi).
    uint32_t image_capacity;
    uint32_t (*now_ms)(void *ctx);
    /// Must not return before the bytes are on their way: a reset requested
    /// right after it must not cut the answer off.
    void (*send)(void *ctx, const uint8_t *bytes, size_t len);
    void (*running)(void *ctx, ota_running_t *out);
    ota_io_t (*erase_inactive)(void *ctx);
    /// len is a multiple of ::OTA_DATA_ALIGN, offset too.
    ota_io_t (*program)(void *ctx, uint32_t offset, const uint8_t *bytes,
                        uint16_t len);
    /// CRC32 of the first size bytes of the inactive bank, CRC-32/ISO-HDLC,
    /// check value 0xCBF43926 (AGREED, spec section 4). Behind the port on
    /// purpose: on the board the L4's hardware CRC unit may compute it, and
    /// that is the board's business, not the state machine's.
    uint32_t (*image_crc32)(void *ctx, uint32_t size);
    ota_io_t (*select_new_bank)(void *ctx);
    ota_io_t (*confirm)(void *ctx);
    /// Called after the ACK to COMMIT has been sent.
    void (*request_reset)(void *ctx);
} ota_update_port_t;

/// Where a transfer stands.
typedef enum {
    OTA_SESSION_IDLE = 0,  ///< Normal mode: telemetry runs.
    OTA_SESSION_RECEIVING, ///< Between BEGIN and COMMIT.
    OTA_SESSION_COMMITTED  ///< COMMIT accepted, reset requested.
} ota_session_t;

/// Largest answer the board sends: INFO.
#define OTA_ANSWER_MAX (OTA_FRAME_OVERHEAD + OTA_INFO_LEN)

/// State carried between frames.
typedef struct {
    const ota_update_port_t *port;
    ota_session_t session;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t next_offset;     ///< Image bytes written so far.
    uint32_t last_answer_ms;  ///< When the board last answered, for timeout.
    /* The last good answer, resent as is when the same frame comes again. */
    uint32_t have_last;
    uint8_t last_type;
    uint16_t last_seq;
    uint8_t last_answer[OTA_ANSWER_MAX];
    size_t last_answer_len;
} ota_update_t;

/// Start idle, with nothing remembered.
void ota_update_init(ota_update_t *update, const ota_update_port_t *port);

/**
 * @brief Handle one frame from the parser. ctx is the ::ota_update_t.
 *
 * Same shape as ::ota_frame_sink_t, so it is passed to
 * ota_frame_parser_feed() as is.
 *
 * A frame with a bad checksum is answered NAK BAD_CRC only during a session.
 * Outside one, a corrupted frame is almost always line noise, and answering it
 * would put binary bytes into the telemetry stream for nothing; the host
 * resends on its own timeout anyway.
 *
 * A frame arriving again with the same TYPE and SEQ as the last one answered
 * OK gets that same answer again and changes nothing: a lost ACK must not make
 * the board erase or write twice (section 6).
 */
void ota_update_on_frame(const ota_frame_t *frame, ota_frame_status_t status,
                         void *ctx);

/// Call from the main loop: ends a session that has been silent too long.
void ota_update_tick(ota_update_t *update);

/// 1 while a transfer is running and telemetry must stay quiet (section 5).
uint32_t ota_update_in_session(const ota_update_t *update);

#endif /* OTA_UPDATE_H */
