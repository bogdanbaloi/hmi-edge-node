/**
 * @file core.c
 * @brief Cortex-M4 core bring-up. The only place SCB registers are touched.
 */

#include "core.h"
#include "registers.h"

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
