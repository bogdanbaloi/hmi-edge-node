/**
 * @file ota_frame_test.c
 * @brief Contract and unit test for the UART flash protocol envelope.
 *
 * The protocol is shared with industrial-hmi, which writes the other side.
 * Both sides must produce the same bytes, and nothing in a compiler checks
 * that. So, as with the telemetry contract test, the anchors here are copied
 * VERBATIM from the agreed spec, industrial-hmi `docs/protocols/uart-flash-v1.md`
 * (status AGREED, 2026-09-22), not derived from this firmware's own code:
 *
 *   - the CRC variant's check value, 0x29B1 over "123456789" (section 3);
 *   - the two worked frames, INFO_REQ and its ACK (section 3).
 *
 * If this code and the spec ever disagree, those tests go red, instead of the
 * two sides drifting apart one plausible byte at a time.
 *
 * The rest pins the parser's behaviour on a noisy line: bytes before a frame,
 * a corrupted checksum, and the resync rule (section 9, item 8), including the
 * reset byte 0xFF measured on the real board on 2026-09-22.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "crc16.h"
#include "ota_frame.h"

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

/* ---- a capturing sink -------------------------------------------------- */

#define CAPTURE_MAX 8U

typedef struct {
    unsigned count;
    unsigned ok;
    unsigned bad_crc;
    ota_frame_status_t status[CAPTURE_MAX];
    uint8_t type[CAPTURE_MAX];
    uint16_t seq[CAPTURE_MAX];
    uint16_t len[CAPTURE_MAX];
    uint8_t payload[CAPTURE_MAX][OTA_FRAME_MAX_PAYLOAD];
} capture_t;

static capture_t g_cap;

/// Copies each frame, because its payload is only valid during the call.
static void capture_sink(const ota_frame_t *frame, void *ctx) {
    capture_t *cap = (capture_t *)ctx;
    if (frame->status == OTA_FRAME_OK) {
        cap->ok++;
    } else {
        cap->bad_crc++;
    }
    if (cap->count < CAPTURE_MAX) {
        const unsigned i = cap->count;
        cap->status[i] = frame->status;
        cap->type[i] = frame->type;
        cap->seq[i] = frame->seq;
        cap->len[i] = frame->len;
        (void)memcpy(cap->payload[i], frame->payload, frame->len);
    }
    cap->count++;
}

/**
 * Index of the last captured frame, safe when nothing was captured. Reading
 * g_cap.x[g_cap.count - 1U] with count at 0 reads index 0xFFFFFFFF, which
 * crashed the test binary under a mutant. The crash threw away the buffered
 * FAIL lines with it, so a KILLED mutant was first reported as surviving.
 */
static unsigned last(void) {
    if (g_cap.count == 0U) {
        return 0U;
    }
    return (g_cap.count <= CAPTURE_MAX) ? g_cap.count - 1U : CAPTURE_MAX - 1U;
}

/// Fresh parser and capture, then every byte fed one at a time.
static void feed_all(const uint8_t *bytes, size_t n) {
    ota_frame_parser_t parser;
    ota_frame_init(&parser);
    (void)memset(&g_cap, 0, sizeof g_cap);
    for (size_t i = 0U; i < n; i++) {
        ota_frame_feed(&parser, bytes[i], capture_sink, &g_cap);
    }
}

/* ---- the anchors, copied verbatim from uart-flash-v1.md, section 3 ----- */

/// "its standard check value over the ASCII string 123456789 is 0x29B1"
#define SPEC_CRC_CHECK_VALUE 0x29B1U

/// "A5  01  01 00  00 00  E9 CD", INFO_REQ as message number 1.
static const uint8_t k_spec_info_req[] = {0xA5, 0x01, 0x01, 0x00,
                                          0x00, 0x00, 0xE9, 0xCD};

/// "A5  82  01 00  00 00  EB 01", the ACK confirming it.
static const uint8_t k_spec_ack[] = {0xA5, 0x82, 0x01, 0x00,
                                     0x00, 0x00, 0xEB, 0x01};

#define TYPE_INFO_REQ 0x01U
#define TYPE_BEGIN    0x02U
#define TYPE_DATA     0x03U
#define TYPE_ACK      0x82U

/// Frame content for the encoder, so each call below stays one line.
static ota_frame_t msg(uint8_t type, uint16_t seq, const uint8_t *payload,
                       uint16_t len) {
    ota_frame_t m;
    m.status = OTA_FRAME_OK;  /* ignored by the encoder */
    m.type = type;
    m.seq = seq;
    m.len = len;
    m.payload = payload;
    return m;
}

/* ---- CRC ---------------------------------------------------------------- */

/// The only proof that the right one of several CRC-16s was picked.
static void crc_reproduces_the_spec_check_value(void) {
    static const char text[] = "123456789";
    CHECK(crc16_compute((const uint8_t *)text, 9U) == SPEC_CRC_CHECK_VALUE);
}

/// Byte by byte and all at once must agree, since the two are used together.
static void crc_update_matches_compute(void) {
    static const uint8_t data[] = {0x01, 0x02, 0xA5, 0xFF, 0x00, 0x7E};
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0U; i < sizeof data; i++) {
        crc = crc16_update(crc, data[i]);
    }
    CHECK(crc == crc16_compute(data, sizeof data));
}

/* ---- encoding: the contract pins ---------------------------------------- */

static void encode_reproduces_the_spec_worked_examples(void) {
    uint8_t out[OTA_FRAME_MAX_SIZE];

    const ota_frame_t info_req = msg(TYPE_INFO_REQ, 1U, NULL, 0U);
    size_t n = ota_frame_encode(&info_req, out, sizeof out);
    CHECK(n == sizeof k_spec_info_req);
    CHECK(memcmp(out, k_spec_info_req, sizeof k_spec_info_req) == 0);

    const ota_frame_t ack = msg(TYPE_ACK, 1U, NULL, 0U);
    n = ota_frame_encode(&ack, out, sizeof out);
    CHECK(n == sizeof k_spec_ack);
    CHECK(memcmp(out, k_spec_ack, sizeof k_spec_ack) == 0);
}

static void encode_refuses_what_cannot_be_a_frame(void) {
    uint8_t out[OTA_FRAME_MAX_SIZE + 1U];
    uint8_t payload[OTA_FRAME_MAX_PAYLOAD + 1U] = {0};

    const ota_frame_t too_long =
        msg(TYPE_DATA, 1U, payload, OTA_FRAME_MAX_PAYLOAD + 1U);
    const ota_frame_t four = msg(TYPE_DATA, 1U, payload, 4U);
    const ota_frame_t no_payload = msg(TYPE_DATA, 1U, NULL, 4U);

    CHECK(ota_frame_encode(&too_long, out, sizeof out) == 0U);
    CHECK(ota_frame_encode(&four, out, 11U) == 0U);  /* needs 12 */
    CHECK(ota_frame_encode(&no_payload, out, sizeof out) == 0U);
    CHECK(ota_frame_encode(&four, NULL, 64U) == 0U);
    CHECK(ota_frame_encode(NULL, out, sizeof out) == 0U);
    CHECK(ota_frame_encode(&four, out, 12U) == 12U);  /* exactly enough */
}

/* ---- parsing ------------------------------------------------------------ */

static void parses_the_spec_worked_examples(void) {
    feed_all(k_spec_info_req, sizeof k_spec_info_req);
    CHECK(g_cap.count == 1U);
    CHECK(g_cap.status[0] == OTA_FRAME_OK);
    CHECK(g_cap.type[0] == TYPE_INFO_REQ);
    CHECK(g_cap.seq[0] == 1U);
    CHECK(g_cap.len[0] == 0U);

    feed_all(k_spec_ack, sizeof k_spec_ack);
    CHECK(g_cap.count == 1U);
    CHECK(g_cap.status[0] == OTA_FRAME_OK);
    CHECK(g_cap.type[0] == TYPE_ACK);
}

/// Nothing is handed over before the last byte, and exactly once after it.
static void a_frame_is_delivered_only_when_complete(void) {
    ota_frame_parser_t parser;
    ota_frame_init(&parser);
    (void)memset(&g_cap, 0, sizeof g_cap);
    for (size_t i = 0U; i + 1U < sizeof k_spec_info_req; i++) {
        ota_frame_feed(&parser, k_spec_info_req[i], capture_sink, &g_cap);
    }
    CHECK(g_cap.count == 0U);
    ota_frame_feed(&parser, k_spec_info_req[sizeof k_spec_info_req - 1U],
                   capture_sink, &g_cap);
    CHECK(g_cap.count == 1U);
}

/// A flipped bit is reported, and never passed on as a good frame.
static void a_bad_checksum_is_reported_not_trusted(void) {
    uint8_t frame[sizeof k_spec_info_req];
    (void)memcpy(frame, k_spec_info_req, sizeof frame);
    frame[2] ^= 0x04U;  /* SEQ corrupted on the wire */
    feed_all(frame, sizeof frame);
    CHECK(g_cap.ok == 0U);
    CHECK(g_cap.bad_crc == 1U);
}

/// Anything before a 0xA5 is dropped: the reset byte, and telemetry text.
static void bytes_before_a_frame_are_skipped(void) {
    uint8_t line[32];
    size_t n = 0U;

    /* The 0xFF every reset put on the line, measured 2026-09-22. */
    line[n++] = 0xFFU;
    (void)memcpy(&line[n], k_spec_info_req, sizeof k_spec_info_req);
    n += sizeof k_spec_info_req;
    feed_all(line, n);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.bad_crc == 0U);

    /* Section 3: 0xA5 never appears in a telemetry line. */
    static const char text[] = "temp,23.5\n";
    n = sizeof text - 1U;
    (void)memcpy(line, text, n);
    (void)memcpy(&line[n], k_spec_info_req, sizeof k_spec_info_req);
    n += sizeof k_spec_info_req;
    feed_all(line, n);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.type[0] == TYPE_INFO_REQ);
}

/**
 * Section 9, item 8, the case the rule was written for: a false 0xA5 whose
 * LEN reads 65535. Waiting for 65535 bytes would swallow every real frame
 * behind it. The frame right after must still arrive.
 */
static void a_huge_false_len_does_not_swallow_the_next_frame(void) {
    uint8_t line[32];
    static const uint8_t false_start[] = {0xA5, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    (void)memcpy(line, false_start, sizeof false_start);
    (void)memcpy(&line[sizeof false_start], k_spec_info_req,
                 sizeof k_spec_info_req);
    feed_all(line, sizeof false_start + sizeof k_spec_info_req);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.type[0] == TYPE_INFO_REQ);
}

/**
 * The same rule on the LEN path, where it is easy to get subtly wrong. A
 * stray 0xA5 sits right before a real frame whose SEQ is 0xFF00 and LEN is 4.
 * Read from the stray byte, those real bytes spell a false LEN of 0x04FF, far
 * over 260. Throwing the whole candidate away would throw the real start byte
 * away with it. Resuming from the byte after the false start keeps it.
 */
static void a_huge_false_len_overlapping_a_real_frame_is_survived(void) {
    static const uint8_t payload[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t line[1U + OTA_FRAME_MAX_SIZE];
    line[0] = OTA_FRAME_START;
    const ota_frame_t m = msg(TYPE_DATA, 0xFF00U, payload, sizeof payload);
    const size_t n = ota_frame_encode(&m, &line[1], sizeof line - 1U);
    feed_all(line, 1U + n);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.seq[last()] == 0xFF00U);
    CHECK(memcmp(g_cap.payload[last()], payload,
                 sizeof payload) == 0);
}

/// A stray 0xA5 right before a real one: the false candidate fails its
/// checksum, the re-scan starts at the next byte, and finds the real frame.
static void a_stray_start_byte_before_a_frame_is_survived(void) {
    uint8_t line[16];
    line[0] = OTA_FRAME_START;
    (void)memcpy(&line[1], k_spec_info_req, sizeof k_spec_info_req);
    feed_all(line, 1U + sizeof k_spec_info_req);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.count >= 1U);
    CHECK(g_cap.status[last()] == OTA_FRAME_OK);
    CHECK(g_cap.seq[last()] == 1U);
}

/**
 * The strongest form of the rule: a false start with a PLAUSIBLE length, 20,
 * so the parser waits for its whole body, and a real frame sits inside that
 * body. Skipping LEN bytes after the checksum fails would lose it. Resuming
 * from the byte after the false start finds it.
 */
static void a_frame_hidden_inside_a_false_frame_is_recovered(void) {
    uint8_t line[40];
    size_t n = 0U;
    static const uint8_t false_header[] = {0xA5, 0x03, 0x00, 0x00, 0x14, 0x00};
    (void)memcpy(line, false_header, sizeof false_header);
    n += sizeof false_header;
    (void)memcpy(&line[n], k_spec_info_req, sizeof k_spec_info_req);
    n += sizeof k_spec_info_req;
    (void)memset(&line[n], 0x00, 12U + 2U);  /* rest of the 20, then CRC */
    n += 12U + 2U;
    feed_all(line, n);
    CHECK(g_cap.bad_crc == 1U);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.type[last()] == TYPE_INFO_REQ);
}

/// 0xA5 inside a payload is data, not a start: the length says so.
static void a_start_byte_inside_a_payload_is_data(void) {
    static const uint8_t payload[] = {0xA5, 0xA5, 0x00, 0xA5, 0xFF, 0xA5};
    uint8_t out[OTA_FRAME_MAX_SIZE];
    const ota_frame_t m = msg(TYPE_BEGIN, 7U, payload, sizeof payload);
    const size_t n = ota_frame_encode(&m, out, sizeof out);
    feed_all(out, n);
    CHECK(g_cap.count == 1U);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.len[0] == sizeof payload);
    CHECK(memcmp(g_cap.payload[0], payload, sizeof payload) == 0);
}

static void back_to_back_frames_both_arrive_in_order(void) {
    uint8_t line[2U * 8U];
    (void)memcpy(line, k_spec_info_req, 8U);
    (void)memcpy(&line[8], k_spec_ack, 8U);
    feed_all(line, sizeof line);
    CHECK(g_cap.ok == 2U);
    CHECK(g_cap.type[0] == TYPE_INFO_REQ);
    CHECK(g_cap.type[1] == TYPE_ACK);
}

/// Noise between two good frames, the reset byte again, costs neither.
static void noise_between_frames_is_skipped(void) {
    uint8_t line[8U + 1U + 8U];
    (void)memcpy(line, k_spec_info_req, 8U);
    line[8] = 0xFFU;
    (void)memcpy(&line[9], k_spec_ack, 8U);
    feed_all(line, sizeof line);
    CHECK(g_cap.ok == 2U);
    CHECK(g_cap.bad_crc == 0U);
    CHECK(g_cap.type[1] == TYPE_ACK);
}

/// 260 is the largest payload the protocol allows, 261 is a false start.
static void the_length_limit_is_exact(void) {
    static uint8_t payload[OTA_FRAME_MAX_PAYLOAD];
    uint8_t out[OTA_FRAME_MAX_SIZE];
    (void)memset(payload, 0x5A, sizeof payload);
    const ota_frame_t m = msg(TYPE_DATA, 3U, payload, OTA_FRAME_MAX_PAYLOAD);
    const size_t n = ota_frame_encode(&m, out, sizeof out);
    CHECK(n == OTA_FRAME_MAX_SIZE);
    feed_all(out, n);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.len[0] == OTA_FRAME_MAX_PAYLOAD);

    static const uint8_t over[] = {0xA5, 0x03, 0x00, 0x00, 0x05, 0x01};
    uint8_t line[16];
    (void)memcpy(line, over, sizeof over);  /* LEN = 0x0105 = 261 */
    (void)memcpy(&line[sizeof over], k_spec_ack, sizeof k_spec_ack);
    feed_all(line, sizeof over + sizeof k_spec_ack);
    CHECK(g_cap.ok == 1U);
    CHECK(g_cap.type[0] == TYPE_ACK);
}

/// Every legal length survives encode then parse, byte for byte.
static void every_length_round_trips(void) {
    static uint8_t payload[OTA_FRAME_MAX_PAYLOAD];
    uint8_t out[OTA_FRAME_MAX_SIZE];
    for (uint16_t len = 0U; len <= OTA_FRAME_MAX_PAYLOAD; len++) {
        for (uint16_t i = 0U; i < len; i++) {
            payload[i] = (uint8_t)((i * 37U + len) & 0xFFU);
        }
        const ota_frame_t m = msg(TYPE_DATA, len, payload, len);
        const size_t n = ota_frame_encode(&m, out, sizeof out);
        feed_all(out, n);
        CHECK(g_cap.ok == 1U && g_cap.bad_crc == 0U && g_cap.len[0] == len &&
              g_cap.seq[0] == len &&
              memcmp(g_cap.payload[0], payload, len) == 0);
    }
}

int main(void) {
    (void)printf("contract: UART flash protocol envelope vs uart-flash-v1.md\n");

    crc_reproduces_the_spec_check_value();
    crc_update_matches_compute();
    encode_reproduces_the_spec_worked_examples();
    encode_refuses_what_cannot_be_a_frame();
    parses_the_spec_worked_examples();
    a_frame_is_delivered_only_when_complete();
    a_bad_checksum_is_reported_not_trusted();
    bytes_before_a_frame_are_skipped();
    a_huge_false_len_does_not_swallow_the_next_frame();
    a_huge_false_len_overlapping_a_real_frame_is_survived();
    a_stray_start_byte_before_a_frame_is_survived();
    a_frame_hidden_inside_a_false_frame_is_recovered();
    a_start_byte_inside_a_payload_is_data();
    back_to_back_frames_both_arrive_in_order();
    noise_between_frames_is_skipped();
    the_length_limit_is_exact();
    every_length_round_trips();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
