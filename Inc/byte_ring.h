#ifndef BYTE_RING_H
#define BYTE_RING_H

#include <stdint.h>

/**
 * @file byte_ring.h
 * @brief A byte queue between one interrupt that writes and one loop that reads.
 *
 * The UART receive interrupt puts each byte in, and the main loop takes them
 * out and feeds the frame parser. The interrupt cannot wait for the loop: the
 * USART holds exactly one received byte, and the next one arrives 87 us later
 * at 115200 baud. So bytes wait here instead.
 *
 * Safe without disabling interrupts, for exactly ONE writer and ONE reader:
 *
 *  - `head` is written only by the writer, `tail` only by the reader. Each
 *    side only reads the other's index, and a 32-bit aligned read or write is
 *    a single access on a Cortex-M4, so it is never seen half done.
 *  - The storage is `volatile` too, so the compiler cannot move the store of a
 *    byte after the store of `head` that publishes it, nor the load of a byte
 *    after the store of `tail` that frees its slot.
 *
 * Both indices run freely and wrap at 2^32. The number of bytes waiting is
 * `head - tail`, which unsigned subtraction keeps right across the wrap, the
 * same rule as the millisecond clock. That needs the capacity to divide 2^32,
 * hence a power of two.
 *
 * Pure logic, no register: the host test drives it on a PC.
 */

/**
 * Bytes it can hold. The largest frame is 268 bytes (a DATA frame), and the
 * host sends the next frame only after the answer to this one, so one frame
 * plus room for the reader being late is enough. The next power of two.
 */
#define BYTE_RING_CAPACITY 512U

/// Masks a free-running index down to a slot.
#define BYTE_RING_INDEX_MASK (BYTE_RING_CAPACITY - 1U)

_Static_assert((BYTE_RING_CAPACITY & BYTE_RING_INDEX_MASK) == 0U,
               "BYTE_RING_CAPACITY must be a power of two");

/// Returned by push and pop when a byte moved.
#define BYTE_RING_MOVED 1U
/// Returned by push when the ring is full, and by pop when it is empty.
#define BYTE_RING_NOTHING 0U

typedef struct {
    volatile uint8_t data[BYTE_RING_CAPACITY];
    volatile uint32_t head;  ///< Written only by the writer: bytes put in.
    volatile uint32_t tail;  ///< Written only by the reader: bytes taken out.
} byte_ring_t;

/// Start empty. Call before the writer can run.
void byte_ring_init(byte_ring_t *ring);

/**
 * @brief Writer side: add one byte.
 *
 * When the ring is full the byte is dropped and the bytes already waiting stay
 * untouched. Overwriting the oldest instead would corrupt a frame that is
 * already half read; dropping the newest leaves a gap the frame CRC catches,
 * and the host resends.
 *
 * @return ::BYTE_RING_MOVED, or ::BYTE_RING_NOTHING when full.
 */
uint32_t byte_ring_push(byte_ring_t *ring, uint8_t byte);

/**
 * @brief Reader side: take the oldest byte.
 * @return ::BYTE_RING_MOVED with the byte in out, or ::BYTE_RING_NOTHING when
 *         empty, leaving out untouched.
 */
uint32_t byte_ring_pop(byte_ring_t *ring, uint8_t *out);

#endif /* BYTE_RING_H */
