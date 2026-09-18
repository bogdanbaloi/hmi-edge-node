/**
 * @file fault.c
 * @brief The fault vectors, and the blink that replaces the silent loop.
 *
 * The startup file declares every handler `.weak` and aliases it to
 * `Default_Handler`. Defining the same symbols here overrides those aliases at
 * link time, with no change to the startup file itself, which stays vendor
 * generated and untouched.
 */

#include "fault.h"

#include "board.h"

/// First blink count. Two, not one: a single blink reads like a reset flicker.
#define FAULT_FIRST_COUNT 2U

/* Timing is a nop loop on purpose. A fault handler must not depend on SysTick
   still running, on interrupts, or on any state the fault may have corrupted.
   These are rough at the 4 MHz reset clock and that is fine: the eye only has
   to tell a blink from a gap. */
#define BLINK_ON_LOOPS   400000UL
#define BLINK_OFF_LOOPS  400000UL
#define BLINK_GAP_LOOPS 2000000UL

uint32_t fault_blink_count(fault_kind_t kind) {
    return FAULT_FIRST_COUNT + (uint32_t)kind;
}

static void fault_wait(volatile uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

/**
 * Blink the count forever. Never returns, and must not: the processor is in a
 * fault state, so there is nothing safe to go back to.
 *
 * `board_init` is called again first. If the fault happened before main got
 * that far, the GPIO clock would still be gated off and the LED would stay
 * dark, which is exactly the silence this whole file exists to remove.
 * Repeating the setup costs a few register writes and makes the signal work
 * whenever the fault lands.
 */
static void fault_trap(fault_kind_t kind) {
    const uint32_t blinks = fault_blink_count(kind);

    board_init();

    for (;;) {
        for (uint32_t i = 0U; i < blinks; i++) {
            board_led_set(1U);
            fault_wait(BLINK_ON_LOOPS);
            board_led_set(0U);
            fault_wait(BLINK_OFF_LOOPS);
        }
        fault_wait(BLINK_GAP_LOOPS);
    }
}

void HardFault_Handler(void)  { fault_trap(FAULT_HARD); }
void MemManage_Handler(void)  { fault_trap(FAULT_MEMMANAGE); }
void BusFault_Handler(void)   { fault_trap(FAULT_BUS); }
void UsageFault_Handler(void) { fault_trap(FAULT_USAGE); }
