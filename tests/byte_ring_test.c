/**
 * @file byte_ring_test.c
 * @brief The queue between the UART receive interrupt and the main loop.
 *
 * On the board the writer is an interrupt and the reader is the main loop.
 * Here both are plain calls, so what this checks is the arithmetic the
 * concurrency relies on: order, full and empty, and the free-running indices
 * crossing 2^32. The ordering of the volatile accesses is argued in
 * byte_ring.h; a PC test cannot race an interrupt, and does not pretend to.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "byte_ring.h"
#include "ota_frame.h"

#include <stdint.h>
#include <stdio.h>

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

/// A byte pop must leave alone when there is nothing to take.
#define UNTOUCHED 0x5AU
/// Keeps a test byte inside uint8_t while still varying with the index.
#define BYTE_OF(i) ((uint8_t)((i) & 0xFFU))
/// A few bytes, enough to cross a boundary without filling the ring.
#define A_FEW 10U
/// How close to 2^32 the wrap tests start, so the crossing lands mid-test.
#define BEFORE_WRAP 3U
/// Laps around the storage for the slot reuse test.
#define LAPS 3U

static byte_ring_t g_ring;

/// Take everything waiting and check it is 0, 1, 2 ... from first.
static void expect_counting_from(uint32_t first, uint32_t count) {
    uint8_t byte = 0U;
    for (uint32_t i = 0U; i < count; i++) {
        CHECK(byte_ring_pop(&g_ring, &byte) == BYTE_RING_MOVED);
        CHECK(byte == BYTE_OF(first + i));
    }
    CHECK(byte_ring_pop(&g_ring, &byte) == BYTE_RING_NOTHING);
}

static void empty_gives_nothing_and_leaves_out_alone(void) {
    byte_ring_init(&g_ring);
    uint8_t byte = UNTOUCHED;
    CHECK(byte_ring_pop(&g_ring, &byte) == BYTE_RING_NOTHING);
    CHECK(byte == UNTOUCHED);
}

static void bytes_come_out_in_the_order_they_went_in(void) {
    byte_ring_init(&g_ring);
    for (uint32_t i = 0U; i < A_FEW; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
    }
    expect_counting_from(0U, A_FEW);
}

/// Full means the NEW byte is dropped and every waiting byte survives: a
/// half-read frame must never be overwritten from behind.
static void full_drops_the_new_byte_and_keeps_the_old_ones(void) {
    byte_ring_init(&g_ring);
    for (uint32_t i = 0U; i < BYTE_RING_CAPACITY; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
    }
    CHECK(byte_ring_push(&g_ring, UNTOUCHED) == BYTE_RING_NOTHING);
    expect_counting_from(0U, BYTE_RING_CAPACITY);
}

static void taking_one_out_of_a_full_ring_frees_one_slot(void) {
    byte_ring_init(&g_ring);
    for (uint32_t i = 0U; i < BYTE_RING_CAPACITY; i++) {
        (void)byte_ring_push(&g_ring, BYTE_OF(i));
    }
    uint8_t byte = 0U;
    CHECK(byte_ring_pop(&g_ring, &byte) == BYTE_RING_MOVED);
    CHECK(byte_ring_push(&g_ring, BYTE_OF(BYTE_RING_CAPACITY)) ==
          BYTE_RING_MOVED);
    CHECK(byte_ring_push(&g_ring, UNTOUCHED) == BYTE_RING_NOTHING);
    expect_counting_from(1U, BYTE_RING_CAPACITY);
}

/// Every slot gets reused several times, and nothing leaks between laps.
static void slots_are_reused_lap_after_lap(void) {
    byte_ring_init(&g_ring);
    uint8_t byte = 0U;
    for (uint32_t i = 0U; i < LAPS * BYTE_RING_CAPACITY; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
        CHECK(byte_ring_pop(&g_ring, &byte) == BYTE_RING_MOVED);
        CHECK(byte == BYTE_OF(i));
    }
}

/// The indices run freely, so after about 4 billion bytes they wrap. A queue
/// that compared them with < instead of subtracting would call itself empty
/// or full at the wrong moment. Started just below 2^32 on purpose.
static void the_index_wrap_changes_nothing(void) {
    g_ring.head = UINT32_MAX - BEFORE_WRAP;
    g_ring.tail = UINT32_MAX - BEFORE_WRAP;
    for (uint32_t i = 0U; i < A_FEW; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
    }
    CHECK(g_ring.head < g_ring.tail);  /* the head really did wrap */
    expect_counting_from(0U, A_FEW);

    g_ring.head = UINT32_MAX - BEFORE_WRAP;
    g_ring.tail = UINT32_MAX - BEFORE_WRAP;
    for (uint32_t i = 0U; i < BYTE_RING_CAPACITY; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
    }
    CHECK(byte_ring_push(&g_ring, UNTOUCHED) == BYTE_RING_NOTHING);
    expect_counting_from(0U, BYTE_RING_CAPACITY);
}

/// The sizing argument in byte_ring.h, checked: the largest frame the host
/// sends fits whole, with the reader not having taken a single byte yet.
static void the_largest_frame_fits_whole(void) {
    byte_ring_init(&g_ring);
    for (uint32_t i = 0U; i < OTA_FRAME_MAX_SIZE; i++) {
        CHECK(byte_ring_push(&g_ring, BYTE_OF(i)) == BYTE_RING_MOVED);
    }
    expect_counting_from(0U, OTA_FRAME_MAX_SIZE);
}

int main(void) {
    (void)printf("unit: byte ring between the UART interrupt and the loop\n");

    empty_gives_nothing_and_leaves_out_alone();
    bytes_come_out_in_the_order_they_went_in();
    full_drops_the_new_byte_and_keeps_the_old_ones();
    taking_one_out_of_a_full_ring_frees_one_slot();
    slots_are_reused_lap_after_lap();
    the_index_wrap_changes_nothing();
    the_largest_frame_fits_whole();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
