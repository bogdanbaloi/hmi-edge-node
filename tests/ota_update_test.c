/**
 * @file ota_update_test.c
 * @brief The update state machine, driven end to end on a PC.
 *
 * The test plays the host agent. Every message is built with the real
 * encoder, pushed byte by byte through the real parser into the state
 * machine, and the board's answers are decoded by a second real parser. So
 * this checks the whole receive-and-answer chain, not the state machine alone.
 *
 * Everything that only exists on the board is a fake behind the port: the
 * inactive bank is a RAM array, the clock is a variable the test moves by
 * hand, and the UART is a capture buffer. Ten minutes of silence costs one
 * assignment, not ten minutes.
 *
 * Every rule checked here is from industrial-hmi `uart-flash-v1.md`,
 * sections 4 to 6 (status AGREED), including the CRC32 variant and the
 * silence timeout pinned on 2026-09-22. One code is not in the spec yet:
 * NAK BAD_MESSAGE, proposed to industrial-hmi on the board.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "ota_frame.h"
#include "ota_frame_parser.h"
#include "ota_update.h"
#include "le_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned g_checks;
static unsigned g_failures;

/// Record one boolean expectation, printing only on failure.
static void check(int condition, const char *what, int line) {
    g_checks++;
    if (!condition) {
        g_failures++;
        (void)printf("  FAIL line %d: %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/* ---- the CRC32 the fake bank reports -------------------------------------
 * CRC-32/ISO-HDLC, AGREED and pinned in the spec, section 4. Behind the
 * port, so the state machine never depends on it; the test asserts the
 * spec's check value so the fake itself is known to be right. */

#define CRC32_POLY_REFLECTED 0xEDB88320U
#define CRC32_INIT           0xFFFFFFFFU
#define CRC32_XOR_OUT        0xFFFFFFFFU
#define BITS_PER_BYTE        8U

/// "check value over the ASCII string 123456789 is 0xCBF43926", section 4.
static const char k_spec_crc32_input[] = "123456789";
#define SPEC_CRC32_CHECK_VALUE 0xCBF43926U

static uint32_t crc32_iso_hdlc(const uint8_t *data, size_t len) {
    uint32_t c = CRC32_INIT;
    for (size_t i = 0U; i < len; i++) {
        c ^= data[i];
        for (uint32_t b = 0U; b < BITS_PER_BYTE; b++) {
            c = ((c & 1U) != 0U) ? (c >> 1) ^ CRC32_POLY_REFLECTED : (c >> 1);
        }
    }
    return c ^ CRC32_XOR_OUT;
}

/* ---- the fake board ----------------------------------------------------- */

/// A small bank, so the whole of it can be checked cheaply.
#define FAKE_CAPACITY 1024U
/// Erased flash reads 0xFF; the bank starts full of something else, so an
/// erase that did not happen shows.
#define ERASED       0xFFU
#define NOT_ERASED   0xAAU

#define TX_MAX 4096U

typedef struct {
    uint8_t bank[FAKE_CAPACITY];
    uint32_t clock_ms;
    uint32_t erase_takes_ms;  ///< The fake erase moves the clock this much.
    ota_io_t erase_result;
    ota_io_t program_result;
    ota_io_t select_result;
    ota_io_t confirm_result;
    ota_running_t running;
    unsigned erase_calls;
    unsigned program_calls;
    unsigned select_calls;
    unsigned confirm_calls;
    unsigned reset_calls;
    unsigned sends;
    unsigned sends_at_reset;  ///< How many answers had gone out at reset.
    uint8_t tx[TX_MAX];
    size_t tx_len;
} fake_t;

static fake_t g_fake;

static uint32_t fake_now(void *ctx) {
    return ((fake_t *)ctx)->clock_ms;
}

static void fake_send(void *ctx, const uint8_t *bytes, size_t len) {
    fake_t *f = (fake_t *)ctx;
    if (f->tx_len + len <= TX_MAX) {
        (void)memcpy(&f->tx[f->tx_len], bytes, len);
        f->tx_len += len;
    }
    f->sends++;
}

static void fake_running(void *ctx, ota_running_t *out) {
    *out = ((fake_t *)ctx)->running;
}

static ota_io_t fake_erase(void *ctx) {
    fake_t *f = (fake_t *)ctx;
    f->erase_calls++;
    f->clock_ms += f->erase_takes_ms;
    if (f->erase_result == OTA_IO_OK) {
        (void)memset(f->bank, ERASED, sizeof f->bank);
    }
    return f->erase_result;
}

static ota_io_t fake_program(void *ctx, uint32_t offset, const uint8_t *bytes,
                             uint16_t len) {
    fake_t *f = (fake_t *)ctx;
    f->program_calls++;
    if (f->program_result == OTA_IO_OK && offset + len <= FAKE_CAPACITY) {
        (void)memcpy(&f->bank[offset], bytes, len);
    }
    return f->program_result;
}

static uint32_t fake_crc32(void *ctx, uint32_t size) {
    return crc32_iso_hdlc(((fake_t *)ctx)->bank, size);
}

static ota_io_t fake_select(void *ctx) {
    fake_t *f = (fake_t *)ctx;
    f->select_calls++;
    return f->select_result;
}

static ota_io_t fake_confirm(void *ctx) {
    fake_t *f = (fake_t *)ctx;
    f->confirm_calls++;
    return f->confirm_result;
}

static void fake_reset(void *ctx) {
    fake_t *f = (fake_t *)ctx;
    f->reset_calls++;
    f->sends_at_reset = f->sends;
}

static const ota_update_port_t k_port = {
    &g_fake,       FAKE_CAPACITY, fake_now,    fake_send,    fake_running,
    fake_erase,    fake_program,  fake_crc32,  fake_select,  fake_confirm,
    fake_reset};

/* ---- the host side: send a message, read the answer --------------------- */

typedef struct {
    unsigned count;
    unsigned bad_crc;
    uint8_t type;
    uint16_t seq;
    uint16_t len;
    uint8_t payload[OTA_INFO_LEN];
} answers_t;

static ota_update_t g_update;
static ota_frame_parser_t g_to_board;
static ota_frame_parser_t g_to_host;
static answers_t g_ans;

/// Keeps the LAST answer, and counts all of them.
static void answer_sink(const ota_frame_t *frame, ota_frame_status_t status,
                        void *ctx) {
    answers_t *a = (answers_t *)ctx;
    if (status != OTA_FRAME_OK) {
        a->bad_crc++;
        return;
    }
    a->count++;
    a->type = frame->type;
    a->seq = frame->seq;
    a->len = frame->len;
    if (frame->len <= sizeof a->payload) {
        (void)memcpy(a->payload, frame->payload, frame->len);
    }
}

static void setup(void) {
    (void)memset(&g_fake, 0, sizeof g_fake);
    (void)memset(g_fake.bank, NOT_ERASED, sizeof g_fake.bank);
    g_fake.running.image_state = OTA_IMAGE_CONFIRMED;
    ota_update_init(&g_update, &k_port);
    ota_frame_parser_init(&g_to_board);
    ota_frame_parser_init(&g_to_host);
}

/// Feeds raw bytes to the board, then decodes whatever it answered.
static void wire(const uint8_t *bytes, size_t n) {
    const size_t before = g_fake.tx_len;
    (void)memset(&g_ans, 0, sizeof g_ans);
    for (size_t i = 0U; i < n; i++) {
        ota_frame_parser_feed(&g_to_board, bytes[i], ota_update_on_frame,
                              &g_update);
    }
    for (size_t i = before; i < g_fake.tx_len; i++) {
        ota_frame_parser_feed(&g_to_host, g_fake.tx[i], answer_sink, &g_ans);
    }
}

/// One host message, built with the real encoder.
static void host(uint8_t type, uint16_t seq, const uint8_t *payload,
                 uint16_t len) {
    const ota_frame_t m = {type, seq, len, payload};
    uint8_t bytes[OTA_FRAME_MAX_SIZE];
    wire(bytes, ota_frame_encode(&m, bytes, sizeof bytes));
}

static void begin(uint16_t seq, uint32_t size, uint32_t crc32) {
    uint8_t p[OTA_BEGIN_LEN] = {0};
    le_write32(&p[OTA_BEGIN_SIZE_AT], size);
    le_write32(&p[OTA_BEGIN_CRC32_AT], crc32);
    host(OTA_MSG_BEGIN, seq, p, sizeof p);
}

static void data(uint16_t seq, uint32_t offset, const uint8_t *bytes,
                 uint16_t n) {
    uint8_t p[OTA_FRAME_MAX_PAYLOAD];
    le_write32(&p[OTA_DATA_OFFSET_AT], offset);
    (void)memcpy(&p[OTA_DATA_BYTES_AT], bytes, n);
    host(OTA_MSG_DATA, seq, p, (uint16_t)(OTA_DATA_BYTES_AT + n));
}

static int answered_ack(uint16_t seq) {
    return g_ans.count == 1U && g_ans.type == OTA_MSG_ACK && g_ans.seq == seq;
}

static int answered_nak(uint16_t seq, uint8_t code) {
    return g_ans.count == 1U && g_ans.type == OTA_MSG_NAK &&
           g_ans.seq == seq && g_ans.len == OTA_NAK_LEN &&
           g_ans.payload[0] == code;
}

/* ---- named values the tests use -----------------------------------------
 * SEQ numbers in the tests below are left as literals on purpose: they are
 * the message numbers of a scripted conversation, 1, 2, 3 in order, like
 * line numbers, and naming each one would hide the order it shows. */

/// A running image to report in INFO. Little-endian on the wire, so its low
/// byte goes first and its high byte fourth: section 3.
#define TEST_VERSION          0x01020304U
#define TEST_VERSION_LOW_BYTE 0x04U
#define TEST_VERSION_TOP_BYTE 0x01U
#define TEST_VERSION_TOP_AT   3U
#define TEST_BANK             2U

/// A TYPE that section 4 does not define.
#define NO_SUCH_TYPE 0x42U

/// Flipped into a byte to corrupt it, and written over padding to spoil it.
#define ONE_BIT 0x01U
#define SPOIL   0x00U

/// Bytes of the CRC16 at the end of every frame, to find the last payload
/// byte from the end: it sits just before them.
#define CRC_SIZE 2U

/// The erase at BEGIN takes 8 s; the host then answers 9 s after the ACK,
/// which is 17 s after its own BEGIN: well past 10 s if counted from BEGIN.
#define ERASE_TAKES_MS        8000U
#define HOST_ANSWERS_AFTER_MS 9000U

/// The clock near its top, and two steps: one that stays below the wrap,
/// one that crosses it.
#define CLOCK_TOP            0xFFFFFFFFU
#define ONE_SECOND_MS        1000U
#define STEP_BEFORE_WRAP_MS  500U
#define STEP_PAST_WRAP_MS    4500U

/* ---- an image for the happy path --------------------------------------- */

/// 20 bytes: not a multiple of 8, so the last DATA frame carries padding.
#define IMAGE_SIZE   20U
#define PADDED_SIZE  24U
#define CHUNK        8U
#define IMAGE_SEED   0x31U

static uint8_t g_image[PADDED_SIZE];

static void make_image(void) {
    for (uint32_t i = 0U; i < PADDED_SIZE; i++) {
        g_image[i] = (i < IMAGE_SIZE) ? (uint8_t)(IMAGE_SEED + i) : ERASED;
    }
}

/// BEGIN plus every DATA frame, SEQ 1 upward. Returns the next SEQ.
static uint16_t send_whole_image(void) {
    make_image();
    begin(1U, IMAGE_SIZE, crc32_iso_hdlc(g_image, IMAGE_SIZE));
    uint16_t seq = 2U;
    for (uint32_t ofs = 0U; ofs < PADDED_SIZE; ofs += CHUNK) {
        data(seq, ofs, &g_image[ofs], CHUNK);
        seq++;
    }
    return seq;
}

/* ---- tests -------------------------------------------------------------- */

static void the_fake_crc32_is_the_agreed_variant(void) {
    CHECK(crc32_iso_hdlc((const uint8_t *)k_spec_crc32_input,
                         sizeof k_spec_crc32_input - 1U) ==
          SPEC_CRC32_CHECK_VALUE);
}

static void info_req_reports_the_running_image(void) {
    setup();
    g_fake.running.version = TEST_VERSION;
    g_fake.running.active_bank = TEST_BANK;
    g_fake.running.image_state = OTA_IMAGE_TRIAL;
    host(OTA_MSG_INFO_REQ, 5U, NULL, 0U);
    CHECK(g_ans.count == 1U && g_ans.type == OTA_MSG_INFO);
    CHECK(g_ans.seq == 5U && g_ans.len == OTA_INFO_LEN);
    /* version little-endian, then bank, then state: section 4 */
    CHECK(g_ans.payload[OTA_INFO_VERSION_AT] == TEST_VERSION_LOW_BYTE &&
          g_ans.payload[OTA_INFO_VERSION_AT + TEST_VERSION_TOP_AT] ==
              TEST_VERSION_TOP_BYTE);
    CHECK(g_ans.payload[OTA_INFO_BANK_AT] == TEST_BANK &&
          g_ans.payload[OTA_INFO_STATE_AT] == OTA_IMAGE_TRIAL);
    CHECK(ota_update_in_session(&g_update) == 0U);
}

static void a_whole_update_goes_through(void) {
    setup();
    const uint16_t next = send_whole_image();
    CHECK(g_fake.erase_calls == 1U);
    CHECK(g_fake.program_calls == PADDED_SIZE / CHUNK);
    CHECK(memcmp(g_fake.bank, g_image, PADDED_SIZE) == 0);
    CHECK(ota_update_in_session(&g_update) == 1U);  /* telemetry quiet */

    host(OTA_MSG_COMMIT, next, NULL, 0U);
    CHECK(answered_ack(next));
    CHECK(g_fake.select_calls == 1U);
    CHECK(g_fake.reset_calls == 1U);
    /* The ACK went out before the reset was asked for. */
    CHECK(g_fake.sends_at_reset == g_fake.sends);
}

static void data_before_begin_is_refused(void) {
    setup();
    data(1U, 0U, g_image, CHUNK);
    CHECK(answered_nak(1U, OTA_NAK_BAD_STATE));
    CHECK(g_fake.program_calls == 0U);
}

static void an_image_too_large_is_refused_before_erasing(void) {
    setup();
    begin(1U, FAKE_CAPACITY + 1U, 0U);
    CHECK(answered_nak(1U, OTA_NAK_TOO_LARGE));
    CHECK(g_fake.erase_calls == 0U);
    begin(2U, FAKE_CAPACITY, 0U);  /* exactly the capacity fits */
    CHECK(answered_ack(2U));
}

static void offsets_must_follow_on_exactly(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    data(2U, CHUNK, g_image, CHUNK);  /* a gap: skipped the first 8 */
    CHECK(answered_nak(2U, OTA_NAK_BAD_OFFSET));
    data(3U, 0U, g_image, CHUNK);
    CHECK(answered_ack(3U));
    data(4U, 0U, g_image, CHUNK);  /* an overlap, under a new SEQ */
    CHECK(answered_nak(4U, OTA_NAK_BAD_OFFSET));
    data(5U, CHUNK, g_image, CHUNK);
    data(6U, 2U * CHUNK, g_image, CHUNK);
    data(7U, 3U * CHUNK, g_image, CHUNK);  /* past the padded size */
    CHECK(answered_nak(7U, OTA_NAK_BAD_OFFSET));
    CHECK(g_fake.program_calls == 3U);
}

static void malformed_messages_get_their_own_answer(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    data(2U, 0U, g_image, CHUNK - 1U);  /* not a multiple of 8 */
    CHECK(answered_nak(2U, OTA_NAK_BAD_MESSAGE));
    host(OTA_MSG_DATA, 3U, g_image, OTA_DATA_BYTES_AT);  /* no image bytes */
    CHECK(answered_nak(3U, OTA_NAK_BAD_MESSAGE));
    host(OTA_MSG_INFO_REQ, 4U, g_image, 1U);  /* INFO_REQ has no payload */
    CHECK(answered_nak(4U, OTA_NAK_BAD_MESSAGE));
    host(NO_SUCH_TYPE, 5U, NULL, 0U);
    CHECK(answered_nak(5U, OTA_NAK_BAD_MESSAGE));
    begin(6U, 0U, 0U);  /* an image of no bytes */
    CHECK(answered_nak(6U, OTA_NAK_BAD_MESSAGE));
    CHECK(g_fake.program_calls == 0U);
}

/// Section 6: an ACK is lost, the host resends. Nothing happens twice.
static void a_repeated_frame_is_answered_but_not_redone(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    begin(1U, IMAGE_SIZE, 0U);  /* same BEGIN again: no second erase */
    CHECK(answered_ack(1U));
    CHECK(g_fake.erase_calls == 1U);
    data(2U, 0U, g_image, CHUNK);
    data(2U, 0U, g_image, CHUNK);  /* same DATA again: no second write */
    CHECK(answered_ack(2U));
    CHECK(g_fake.program_calls == 1U);
    host(OTA_MSG_INFO_REQ, 3U, NULL, 0U);
    host(OTA_MSG_INFO_REQ, 3U, NULL, 0U);  /* the SAME answer: INFO */
    CHECK(g_ans.count == 1U && g_ans.type == OTA_MSG_INFO && g_ans.seq == 3U);
}

static void commit_checks_everything_arrived_and_the_crc32(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, crc32_iso_hdlc(g_image, IMAGE_SIZE));
    data(2U, 0U, g_image, CHUNK);
    host(OTA_MSG_COMMIT, 3U, NULL, 0U);  /* 8 of 20 bytes sent */
    CHECK(answered_nak(3U, OTA_NAK_BAD_STATE));

    setup();
    make_image();
    begin(1U, IMAGE_SIZE, crc32_iso_hdlc(g_image, IMAGE_SIZE) ^ ONE_BIT);
    for (uint32_t ofs = 0U; ofs < PADDED_SIZE; ofs += CHUNK) {
        data((uint16_t)(2U + ofs / CHUNK), ofs, &g_image[ofs], CHUNK);
    }
    host(OTA_MSG_COMMIT, 9U, NULL, 0U);
    CHECK(answered_nak(9U, OTA_NAK_VERIFY_FAILED));
    CHECK(g_fake.select_calls == 0U && g_fake.reset_calls == 0U);
    CHECK(ota_update_in_session(&g_update) == 0U);  /* bank never switched */
}

/// Only the image size is checked, never the 0xFF padding (section 4).
static void the_padding_is_outside_the_crc32(void) {
    setup();
    make_image();
    const uint16_t next = send_whole_image();
    g_fake.bank[IMAGE_SIZE] = SPOIL;  /* the padding only */
    host(OTA_MSG_COMMIT, next, NULL, 0U);
    CHECK(answered_ack(next));
}

static void abort_leaves_the_running_image_alone(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    host(OTA_MSG_ABORT, 2U, NULL, 0U);
    CHECK(answered_ack(2U));
    CHECK(ota_update_in_session(&g_update) == 0U);
    CHECK(g_fake.select_calls == 0U && g_fake.reset_calls == 0U);
    data(3U, 0U, g_image, CHUNK);
    CHECK(answered_nak(3U, OTA_NAK_BAD_STATE));
}

static void silence_ends_the_session(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    g_fake.clock_ms += OTA_UPDATE_SILENCE_MS - 1U;
    ota_update_tick(&g_update);
    CHECK(ota_update_in_session(&g_update) == 1U);
    g_fake.clock_ms += 1U;
    ota_update_tick(&g_update);
    CHECK(ota_update_in_session(&g_update) == 0U);
    data(2U, 0U, g_image, CHUNK);
    CHECK(answered_nak(2U, OTA_NAK_BAD_STATE));
}

/**
 * The timeout counts from the board's last ANSWER, not from the last frame.
 * The erase at BEGIN takes 8 s here; the host answers 9 s after the ACK, 17 s
 * after its own BEGIN. Counting from the frame would have given up already.
 */
static void erase_time_does_not_count_against_the_host(void) {
    setup();
    make_image();
    g_fake.erase_takes_ms = ERASE_TAKES_MS;
    begin(1U, IMAGE_SIZE, 0U);
    g_fake.clock_ms += HOST_ANSWERS_AFTER_MS;
    ota_update_tick(&g_update);
    CHECK(ota_update_in_session(&g_update) == 1U);
    data(2U, 0U, g_image, CHUNK);
    CHECK(answered_ack(2U));
}

/**
 * Walks the clock through the 49-day wrap, checking BEFORE and after zero.
 * The addition form, now >= last + silence, fails before the wrap, not after:
 * near the top, last + silence overflows to a small number, and the session
 * would end the instant it began. Checking only after zero misses that, which
 * is how the first version of this test let the mutant through.
 */
static void the_timeout_survives_the_clock_wrap(void) {
    setup();
    make_image();
    g_fake.clock_ms = CLOCK_TOP - ONE_SECOND_MS;
    begin(1U, IMAGE_SIZE, 0U);
    g_fake.clock_ms += STEP_BEFORE_WRAP_MS;  /* still before zero */
    ota_update_tick(&g_update);
    CHECK(ota_update_in_session(&g_update) == 1U);
    g_fake.clock_ms += STEP_PAST_WRAP_MS;  /* now past zero */
    ota_update_tick(&g_update);
    CHECK(ota_update_in_session(&g_update) == 1U);
}

static void flash_failures_end_the_session(void) {
    setup();
    g_fake.erase_result = OTA_IO_FAILED;
    begin(1U, IMAGE_SIZE, 0U);
    CHECK(answered_nak(1U, OTA_NAK_FLASH_ERROR));
    CHECK(ota_update_in_session(&g_update) == 0U);

    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    g_fake.program_result = OTA_IO_FAILED;
    data(2U, 0U, g_image, CHUNK);
    CHECK(answered_nak(2U, OTA_NAK_FLASH_ERROR));
    CHECK(ota_update_in_session(&g_update) == 0U);
}

/// A corrupted frame is answered during a session, and ignored outside it.
static void a_bad_checksum_is_answered_only_in_a_session(void) {
    setup();
    const ota_frame_t m = {OTA_MSG_INFO_REQ, 1U, 0U, NULL};
    uint8_t bytes[OTA_FRAME_MAX_SIZE];
    const size_t n = ota_frame_encode(&m, bytes, sizeof bytes);
    bytes[n - 1U] ^= ONE_BIT;  /* the last byte, part of the CRC16 */
    wire(bytes, n);
    CHECK(g_ans.count == 0U);  /* idle: silence, not binary in telemetry */

    make_image();
    begin(2U, IMAGE_SIZE, 0U);
    wire(bytes, n);
    CHECK(answered_nak(1U, OTA_NAK_BAD_CRC));
}

static void confirm_keeps_a_trial_image(void) {
    setup();
    g_fake.running.image_state = OTA_IMAGE_TRIAL;
    host(OTA_MSG_CONFIRM, 1U, NULL, 0U);
    CHECK(answered_ack(1U));
    CHECK(g_fake.confirm_calls == 1U);

    setup();  /* already confirmed: ACK, nothing written */
    host(OTA_MSG_CONFIRM, 1U, NULL, 0U);
    CHECK(answered_ack(1U));
    CHECK(g_fake.confirm_calls == 0U);

    setup();  /* not in the middle of a transfer */
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    host(OTA_MSG_CONFIRM, 2U, NULL, 0U);
    CHECK(answered_nak(2U, OTA_NAK_BAD_STATE));
}

/// A second BEGIN restarts: there is no resume (section 8).
static void a_new_begin_starts_over(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    data(2U, 0U, g_image, CHUNK);
    begin(3U, IMAGE_SIZE, 0U);
    CHECK(answered_ack(3U));
    CHECK(g_fake.erase_calls == 2U);
    data(4U, 0U, g_image, CHUNK);  /* offset 0 is expected again */
    CHECK(answered_ack(4U));
}

/**
 * A repeat is the same TYPE and SEQ, not SEQ alone. If the host agent
 * restarts, its SEQ starts over at 1: an INFO_REQ numbered 1 must get INFO,
 * not the remembered ACK of an older BEGIN that was also numbered 1.
 */
static void a_repeat_needs_the_same_type_not_just_the_same_seq(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    host(OTA_MSG_INFO_REQ, 1U, NULL, 0U);
    CHECK(g_ans.count == 1U && g_ans.type == OTA_MSG_INFO);
}

/**
 * A NAK is never remembered. A frame corrupted only in its payload keeps its
 * TYPE and SEQ, gets NAK BAD_CRC, and the host resends it clean. Answering the
 * clean copy from memory would repeat the NAK forever and stall the transfer.
 */
static void a_clean_resend_after_a_nak_is_accepted(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    uint8_t p[OTA_DATA_BYTES_AT + CHUNK];
    le_write32(&p[OTA_DATA_OFFSET_AT], 0U);
    (void)memcpy(&p[OTA_DATA_BYTES_AT], g_image, CHUNK);
    const ota_frame_t m = {OTA_MSG_DATA, 2U, sizeof p, p};
    uint8_t bytes[OTA_FRAME_MAX_SIZE];
    const size_t n = ota_frame_encode(&m, bytes, sizeof bytes);
    bytes[n - CRC_SIZE - 1U] ^= ONE_BIT;  /* last payload byte */
    wire(bytes, n);
    CHECK(answered_nak(2U, OTA_NAK_BAD_CRC));
    data(2U, 0U, g_image, CHUNK);  /* the clean resend */
    CHECK(answered_ack(2U));
    CHECK(g_fake.program_calls == 1U);
}

/// After a session ends, a late repeat is judged again, not answered from
/// memory: otherwise the host would think a write happened that did not.
/// The session ends by SILENCE here, on purpose: ending it with ABORT would
/// hide a memory that was never cleared, because the ACK to ABORT overwrites
/// it. That is how the first version of this test let the mutant through.
static void a_late_repeat_after_the_session_is_judged_again(void) {
    setup();
    make_image();
    begin(1U, IMAGE_SIZE, 0U);
    data(2U, 0U, g_image, CHUNK);
    g_fake.clock_ms += OTA_UPDATE_SILENCE_MS;
    ota_update_tick(&g_update);
    data(2U, 0U, g_image, CHUNK);  /* the same DATA, arriving too late */
    CHECK(answered_nak(2U, OTA_NAK_BAD_STATE));
    CHECK(g_fake.program_calls == 1U);
}

/// Every answer the board sent in this run was a well-formed frame.
static void every_answer_was_a_good_frame(void) {
    setup();
    (void)send_whole_image();
    ota_frame_parser_t p;
    answers_t all;
    ota_frame_parser_init(&p);
    (void)memset(&all, 0, sizeof all);
    for (size_t i = 0U; i < g_fake.tx_len; i++) {
        ota_frame_parser_feed(&p, g_fake.tx[i], answer_sink, &all);
    }
    CHECK(all.bad_crc == 0U);
    CHECK(all.count == 1U + PADDED_SIZE / CHUNK);
}

int main(void) {
    (void)printf("unit: OTA update state machine, host to board and back\n");

    the_fake_crc32_is_the_agreed_variant();
    info_req_reports_the_running_image();
    a_whole_update_goes_through();
    data_before_begin_is_refused();
    an_image_too_large_is_refused_before_erasing();
    offsets_must_follow_on_exactly();
    malformed_messages_get_their_own_answer();
    a_repeated_frame_is_answered_but_not_redone();
    commit_checks_everything_arrived_and_the_crc32();
    the_padding_is_outside_the_crc32();
    abort_leaves_the_running_image_alone();
    silence_ends_the_session();
    erase_time_does_not_count_against_the_host();
    the_timeout_survives_the_clock_wrap();
    flash_failures_end_the_session();
    a_bad_checksum_is_answered_only_in_a_session();
    confirm_keeps_a_trial_image();
    a_new_begin_starts_over();
    a_repeat_needs_the_same_type_not_just_the_same_seq();
    a_clean_resend_after_a_nak_is_accepted();
    a_late_repeat_after_the_session_is_judged_again();
    every_answer_was_a_good_frame();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
