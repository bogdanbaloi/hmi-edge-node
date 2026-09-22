/**
 * @file ota_update.c
 * @brief The update state machine, one handler per message type.
 *
 * Every handler checks in the same order, so the rule is predictable:
 *
 *   1. shape: can this message be understood at all?     NAK BAD_MESSAGE
 *   2. state: is it allowed right now?                    NAK BAD_STATE
 *   3. content: offsets, sizes, checksums                 the specific NAK
 *
 * Only then does it act, and it answers ACK (or INFO) last.
 */

#include "ota_update.h"

#include <string.h>

#include "le_bytes.h"


/* Whether an answer is kept for a repeated frame. NAKs never are: a resend
   after a NAK must be judged again, because a retry can succeed. */
#define KEEP_FOR_REPEAT 1U
#define DO_NOT_KEEP     0U

/// What check_data() returns when a DATA frame is fine.
#define DATA_OK 0U

static uint32_t now(const ota_update_t *update) {
    return update->port->now_ms(update->port->ctx);
}

/// Back to normal mode, forgetting the last answer: a late repeat from the
/// ended session must be judged again, not answered from memory.
static void end_session(ota_update_t *update) {
    update->session = OTA_SESSION_IDLE;
    update->next_offset = 0U;
    update->have_last = 0U;
}

static void reply(ota_update_t *update, const ota_frame_t *request,
                  const ota_frame_t *answer, uint32_t keep) {
    uint8_t bytes[OTA_ANSWER_MAX];
    const size_t n = ota_frame_encode(answer, bytes, sizeof bytes);
    if (n == OTA_FRAME_ENCODE_REFUSED) {
        return;
    }
    update->port->send(update->port->ctx, bytes, n);
    update->last_answer_ms = now(update);
    if (keep == KEEP_FOR_REPEAT) {
        update->have_last = 1U;
        update->last_type = request->type;
        update->last_seq = request->seq;
        (void)memcpy(update->last_answer, bytes, n);
        update->last_answer_len = n;
    }
}

static void ack(ota_update_t *update, const ota_frame_t *request) {
    const ota_frame_t answer = {OTA_MSG_ACK, request->seq, 0U, NULL};
    reply(update, request, &answer, KEEP_FOR_REPEAT);
}

static void nak(ota_update_t *update, const ota_frame_t *request,
                uint8_t code) {
    const ota_frame_t answer = {OTA_MSG_NAK, request->seq, OTA_NAK_LEN, &code};
    reply(update, request, &answer, DO_NOT_KEEP);
}

static void on_info_req(ota_update_t *update, const ota_frame_t *frame) {
    if (frame->len != 0U) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    ota_running_t running;
    update->port->running(update->port->ctx, &running);
    uint8_t payload[OTA_INFO_LEN];
    le_write32(&payload[OTA_INFO_VERSION_AT], running.version);
    payload[OTA_INFO_BANK_AT] = running.active_bank;
    payload[OTA_INFO_STATE_AT] = running.image_state;
    const ota_frame_t answer = {OTA_MSG_INFO, frame->seq, OTA_INFO_LEN,
                                payload};
    reply(update, frame, &answer, KEEP_FOR_REPEAT);
}

/// A new BEGIN in the middle of a transfer starts over: the spec has no
/// resume (section 8), so the host restarts from BEGIN.
static void on_begin(ota_update_t *update, const ota_frame_t *frame) {
    if (frame->len != OTA_BEGIN_LEN) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    const uint32_t size = le_read32(&frame->payload[OTA_BEGIN_SIZE_AT]);
    if (size == 0U) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    if (update->session == OTA_SESSION_COMMITTED) {
        nak(update, frame, OTA_NAK_BAD_STATE);
        return;
    }
    if (size > update->port->image_capacity) {
        nak(update, frame, OTA_NAK_TOO_LARGE);
        return;
    }
    if (update->port->erase_inactive(update->port->ctx) != OTA_IO_OK) {
        end_session(update);
        nak(update, frame, OTA_NAK_FLASH_ERROR);
        return;
    }
    update->session = OTA_SESSION_RECEIVING;
    update->image_size = size;
    update->image_crc32 = le_read32(&frame->payload[OTA_BEGIN_CRC32_AT]);
    update->next_offset = 0U;
    ack(update, frame);
}

/// The image size rounded up to whole DATA units: the last frame is padded
/// with 0xFF, so it may run past the size, never past this.
static uint32_t padded_size(const ota_update_t *update) {
    const uint32_t rest = update->image_size % OTA_DATA_ALIGN;
    return (rest == 0U) ? update->image_size
                        : update->image_size + (OTA_DATA_ALIGN - rest);
}

/// Returns ::DATA_OK, or the NAK code that refuses this DATA frame.
static uint8_t check_data(const ota_update_t *update,
                          const ota_frame_t *frame) {
    if (frame->len <= OTA_DATA_BYTES_AT) {
        return OTA_NAK_BAD_MESSAGE;  /* an offset and no image bytes */
    }
    const uint32_t count = (uint32_t)frame->len - OTA_DATA_BYTES_AT;
    if (count % OTA_DATA_ALIGN != 0U) {
        return OTA_NAK_BAD_MESSAGE;
    }
    if (update->session != OTA_SESSION_RECEIVING) {
        return OTA_NAK_BAD_STATE;
    }
    const uint32_t offset = le_read32(&frame->payload[OTA_DATA_OFFSET_AT]);
    const uint32_t limit = padded_size(update);
    if (offset != update->next_offset || count > limit - offset) {
        return OTA_NAK_BAD_OFFSET;  /* a gap, an overlap, or past the end */
    }
    return DATA_OK;
}

static void on_data(ota_update_t *update, const ota_frame_t *frame) {
    const uint8_t problem = check_data(update, frame);
    if (problem != DATA_OK) {
        nak(update, frame, problem);
        return;
    }
    const uint16_t count = (uint16_t)(frame->len - OTA_DATA_BYTES_AT);
    if (update->port->program(update->port->ctx, update->next_offset,
                              &frame->payload[OTA_DATA_BYTES_AT],
                              count) != OTA_IO_OK) {
        end_session(update);
        nak(update, frame, OTA_NAK_FLASH_ERROR);
        return;
    }
    update->next_offset += count;
    ack(update, frame);
}

/// The ACK goes out BEFORE the reset is requested, or the host would never
/// hear that its image was accepted.
static void on_commit(ota_update_t *update, const ota_frame_t *frame) {
    if (frame->len != 0U) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    if (update->session != OTA_SESSION_RECEIVING ||
        update->next_offset < update->image_size) {
        nak(update, frame, OTA_NAK_BAD_STATE);  /* nothing, or not all, sent */
        return;
    }
    if (update->port->image_crc32(update->port->ctx, update->image_size) !=
        update->image_crc32) {
        end_session(update);
        nak(update, frame, OTA_NAK_VERIFY_FAILED);
        return;
    }
    if (update->port->select_new_bank(update->port->ctx) != OTA_IO_OK) {
        end_session(update);
        nak(update, frame, OTA_NAK_FLASH_ERROR);
        return;
    }
    update->session = OTA_SESSION_COMMITTED;
    ack(update, frame);
    update->port->request_reset(update->port->ctx);
}

/// Confirming an image that is already confirmed is harmless, so it is
/// answered ACK too: the host may simply not have heard the first ACK.
static void on_confirm(ota_update_t *update, const ota_frame_t *frame) {
    if (frame->len != 0U) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    if (update->session != OTA_SESSION_IDLE) {
        nak(update, frame, OTA_NAK_BAD_STATE);
        return;
    }
    ota_running_t running;
    update->port->running(update->port->ctx, &running);
    if (running.image_state == OTA_IMAGE_TRIAL &&
        update->port->confirm(update->port->ctx) != OTA_IO_OK) {
        nak(update, frame, OTA_NAK_FLASH_ERROR);
        return;
    }
    ack(update, frame);
}

static void on_abort(ota_update_t *update, const ota_frame_t *frame) {
    if (frame->len != 0U) {
        nak(update, frame, OTA_NAK_BAD_MESSAGE);
        return;
    }
    if (update->session == OTA_SESSION_COMMITTED) {
        nak(update, frame, OTA_NAK_BAD_STATE);  /* too late, bank selected */
        return;
    }
    end_session(update);
    ack(update, frame);
}

static void dispatch(ota_update_t *update, const ota_frame_t *frame) {
    switch (frame->type) {
    case OTA_MSG_INFO_REQ: on_info_req(update, frame); break;
    case OTA_MSG_BEGIN:    on_begin(update, frame);    break;
    case OTA_MSG_DATA:     on_data(update, frame);     break;
    case OTA_MSG_COMMIT:   on_commit(update, frame);   break;
    case OTA_MSG_CONFIRM:  on_confirm(update, frame);  break;
    case OTA_MSG_ABORT:    on_abort(update, frame);    break;
    default:               nak(update, frame, OTA_NAK_BAD_MESSAGE); break;
    }
}

static uint32_t is_repeat(const ota_update_t *update,
                          const ota_frame_t *frame) {
    return (update->have_last != 0U && frame->type == update->last_type &&
            frame->seq == update->last_seq)
               ? 1U
               : 0U;
}

void ota_update_init(ota_update_t *update, const ota_update_port_t *port) {
    (void)memset(update, 0, sizeof *update);
    update->port = port;
    update->session = OTA_SESSION_IDLE;
}

void ota_update_on_frame(const ota_frame_t *frame, ota_frame_status_t status,
                         void *ctx) {
    ota_update_t *update = (ota_update_t *)ctx;
    if (status != OTA_FRAME_OK) {
        if (update->session != OTA_SESSION_IDLE) {
            nak(update, frame, OTA_NAK_BAD_CRC);
        }
        return;
    }
    if (is_repeat(update, frame) != 0U) {
        /* Same frame again: the same answer, and nothing done twice. */
        update->port->send(update->port->ctx, update->last_answer,
                           update->last_answer_len);
        update->last_answer_ms = now(update);
        return;
    }
    dispatch(update, frame);
}

void ota_update_tick(ota_update_t *update) {
    /* Subtraction, not addition: correct across the 49-day clock wrap, the
       same rule as the debounce in telemetry. */
    if (update->session == OTA_SESSION_RECEIVING &&
        now(update) - update->last_answer_ms >= OTA_UPDATE_SILENCE_MS) {
        end_session(update);
    }
}

uint32_t ota_update_in_session(const ota_update_t *update) {
    return (update->session != OTA_SESSION_IDLE) ? 1U : 0U;
}
