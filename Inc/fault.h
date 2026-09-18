#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

/**
 * @file fault.h
 * @brief What the board does when it faults, instead of going quiet.
 *
 * The startup file points every fault vector at `Default_Handler`, which is
 * `b Infinite_Loop`. So today a hard fault looks exactly like a working board
 * that happens to be idle: LED unchanged, serial silent, nothing to see. You
 * find out something is wrong only because the button stopped doing anything.
 *
 * That is the last silent failure left in this firmware, and the same class as
 * the ones already closed: raw counts labelled as degrees, a temperature that
 * was wrong by thirty degrees, an FPU that faults on the first float. Every
 * one of those was fixed by making the failure say something.
 *
 * So the handlers here blink a COUNT on the LED, forever. Two blinks is a hard
 * fault, three a memory management fault, four a bus fault, five a usage
 * fault. You count them from across the room and you know what happened,
 * without a debugger attached.
 */

/// The four faults that have their own vector on a Cortex-M4.
typedef enum {
    FAULT_HARD = 0,   ///< Escalated from anything unhandled, the usual one.
    FAULT_MEMMANAGE,  ///< MPU violation. No MPU is configured here.
    FAULT_BUS,        ///< Bad address on the bus, often a wild pointer.
    FAULT_USAGE       ///< Undefined instruction, or a coprocessor that is off.
} fault_kind_t;

/**
 * @brief How many blinks identify this fault.
 *
 * Pure arithmetic, so a host test can check the one property that matters:
 * every kind maps to a DIFFERENT count, and every count is small enough that
 * a person can actually count it. A pattern nobody can tell apart from another
 * pattern is the same as no pattern.
 *
 * Counts start at two, never one: a single blink is hard to distinguish from
 * a board that flickered while resetting.
 */
uint32_t fault_blink_count(fault_kind_t kind);

#endif /* FAULT_H */
