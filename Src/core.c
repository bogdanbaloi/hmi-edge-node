/**
 * @file core.c
 * @brief Cortex-M4 core bring-up. The only place SCB registers are touched.
 */

#include "core.h"
#include "registers.h"

static uint32_t g_millis;    ///< Whole milliseconds counted so far.
static uint32_t g_last_cvr;  ///< SysTick value at the previous reading.
static uint32_t g_leftover;  ///< Ticks not yet worth a millisecond.

void core_tick_init(void) {
    /* Free running at full 24-bit range, NOT reloading every millisecond.
       The obvious design is a 1 ms reload plus the COUNTFLAG bit, but that bit
       only says "reached zero at least once since you last read it", never how
       many times. uart_send_string blocks for about 2.7 ms per press at
       115200, and during that nobody reads the flag, so the clock would lose
       those milliseconds SILENTLY and run slow. Counting ticks instead makes a
       long gap add the right number of milliseconds. */
    SYST_RVR = SYST_COUNTER_MAX;
    SYST_CVR = 0U;  /* any write clears the counter */
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;

    g_millis   = 0U;
    g_last_cvr = SYST_CVR & SYST_COUNTER_MAX;
    g_leftover = 0U;
}

uint32_t core_millis(void) {
    const uint32_t cvr = SYST_CVR & SYST_COUNTER_MAX;

    /* SysTick counts DOWN, so ticks elapsed is previous minus current. The
       mask makes the subtraction correct across a wrap, the same reason the
       callers of this function must compare with subtraction too. */
    const uint32_t elapsed = (g_last_cvr - cvr) & SYST_COUNTER_MAX;
    g_last_cvr = cvr;

    g_leftover += elapsed;
    g_millis   += g_leftover / SYST_TICKS_PER_MS;
    g_leftover %= SYST_TICKS_PER_MS;

    return g_millis;
}

void core_enable_fpu(void) {
    SCB_CPACR |= SCB_CPACR_FPU_FULL;

    /* DSB then ISB, in that order, as the ARMv7-M manual requires. DSB waits
       for the CPACR write to actually land. ISB flushes the pipeline, so no
       instruction fetched under the old permissions is still in flight when
       the first VFP instruction executes. Without them the enable can appear
       to work and then fault under optimisation or a different core. */
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
