#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/**
 * @file watchdog.h
 * @brief The independent watchdog (IWDG). HAL: the only place its registers
 *        are touched.
 *
 * A watchdog is a counter that resets the board unless the software keeps
 * telling it that it is still alive. It answers the one failure the fault
 * handler cannot: code that does not crash but stops making progress, a loop
 * waiting for something that will never come. A blinking LED needs a fault;
 * a hung loop raises none.
 *
 * Three facts decide how it is used here (RM0351 Rev 9, section 36):
 *
 *  - It runs from the LSI, its own 32 kHz oscillator, so it survives a main
 *    clock that has died.
 *  - **Once started it cannot be stopped.** Only a reset clears it. So it is
 *    started deliberately, late in start-up, after everything it depends on.
 *  - Its longest period is about 32.8 s, which is what this firmware uses:
 *    the loop feeds it every pass and the longest thing the loop ever does
 *    is verify a full image, measured at well under a second.
 *
 * Bogdan decided on 2026-09-22 that it runs in the CONFIRMED state too, not
 * only during a trial: a board stuck in the fault handler then reboots after
 * about 32 s instead of blinking forever.
 */

/// The longest period the IWDG can be given, in milliseconds: (4095 + 1)
/// ticks of the 32 kHz LSI divided by 256. Documentation, not a setting.
#define WATCHDOG_PERIOD_MS 32768U

/**
 * @brief Start the watchdog at its longest period. Cannot be undone.
 *
 * Call once, after the drivers are up and just before the main loop, so that
 * a hang during start-up cannot be masked by a watchdog that is not running
 * yet, and so nothing long-running happens before the first feed.
 */
void watchdog_start(void);

/// Tell it we are still alive. Call from the main loop, every pass.
void watchdog_feed(void);

/**
 * @brief Did the watchdog cause the last reset?
 *
 * Reads the flag RCC keeps across a reset and clears it, so the answer is
 * given once. Called at start-up, before the flag is lost.
 *
 * @return 1 if the last reset came from the watchdog, 0 otherwise.
 */
uint32_t watchdog_caused_last_reset(void);

#endif /* WATCHDOG_H */
