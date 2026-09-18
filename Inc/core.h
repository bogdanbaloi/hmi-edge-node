#ifndef CORE_H
#define CORE_H

/**
 * @file core.h
 * @brief Cortex-M4 core bring-up. Not an STM32 peripheral, not board specific.
 *
 * `board` owns what changes when you swap the board, `uart` and `adc` own
 * their peripherals. This file owns the CPU core itself, which is identical on
 * every Cortex-M4 and described by the ARMv7-M Architecture Reference Manual
 * rather than by RM0351. Keeping it separate is what stops core bring-up from
 * leaking into the board file, where it would be wrong the moment the board
 * changes.
 */

/**
 * @brief Turn the FPU on. Must run before any floating point code.
 *
 * An FPU has TWO switches, and only one of them is a compiler flag:
 *
 *   1. Build time: `-mfloat-abi=hard -mfpu=fpv4-sp-d16` tells the compiler an
 *      FPU is present, so float code becomes VFP instructions.
 *   2. Run time: CPACR grants access to coprocessors 10 and 11. It resets to
 *      "denied", so the FPU is OFF after every reset.
 *
 * With switch 1 on and switch 2 missing, a float compiles silently, hits a
 * disabled unit, raises a UsageFault with the NOCP bit, escalates to
 * HardFault, and lands in the startup file's default handler, which is an
 * infinite loop. The board just freezes, with nothing on the serial line.
 *
 * Nothing catches that mismatch: not the compiler, not the linker, and not the
 * host tests, because a PC has a working FPU. Only the board fails, and it
 * fails by going quiet. So the switch gets flipped once, as early as possible.
 */
void core_enable_fpu(void);

#endif /* CORE_H */
