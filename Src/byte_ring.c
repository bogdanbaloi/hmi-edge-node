/**
 * @file byte_ring.c
 * @brief One writer, one reader byte queue. See byte_ring.h for why it is safe.
 */

#include "byte_ring.h"

void byte_ring_init(byte_ring_t *ring) {
    ring->head = 0U;
    ring->tail = 0U;
}

uint32_t byte_ring_push(byte_ring_t *ring, uint8_t byte) {
    const uint32_t head = ring->head;
    if (head - ring->tail == BYTE_RING_CAPACITY) {
        return BYTE_RING_NOTHING;
    }
    ring->data[head & BYTE_RING_INDEX_MASK] = byte;
    ring->head = head + 1U;  /* publishes the byte, so it comes last */
    return BYTE_RING_MOVED;
}

uint32_t byte_ring_pop(byte_ring_t *ring, uint8_t *out) {
    const uint32_t tail = ring->tail;
    if (ring->head == tail) {
        return BYTE_RING_NOTHING;
    }
    *out = ring->data[tail & BYTE_RING_INDEX_MASK];
    ring->tail = tail + 1U;  /* frees the slot, so it comes last */
    return BYTE_RING_MOVED;
}
