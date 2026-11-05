#ifndef BRINGUP_H
#define BRINGUP_H

/**
 * @file bringup.h
 * @brief The switch that turns on the one shot bus diagnostic at start-up.
 *
 * Shaped like arming.h on purpose: one line, in a file with nothing else in
 * it, so turning the diagnostic on or off is a visible diff rather than a
 * setting somebody remembers.
 *
 * **It is off, and anything that ships keeps it off.** It writes lines no
 * host asked for onto a wire another repository parses, and it costs about a
 * second of start-up. Turn it on to find a sensor, turn it off once the
 * sensor is found.
 *
 * WHY IT EXISTS. `adxl,absent` says only that nothing answered at 0x53. That
 * one word is true for a dead bus, for the part sitting at 0x1D with SDO
 * floating, for a part that came up in SPI mode with CS floating, and for a
 * jumper in the hole next door. Four repairs behind one word. On 2026-11-05
 * it was the fourth, and three hours went into reading photographs of the
 * board before the board was asked instead. It answered in one line.
 */

/* #define BRINGUP_DIAGNOSTIC 1 */

/**
 * @brief Measure the two bus lines, name every pin held from outside, then
 *        scan the bus and read ten samples.
 *
 * Needs every driver up and the millisecond clock running, because the I2C
 * timeout is counted in milliseconds. Call it before the watchdog starts: a
 * silent bus costs 5 ms per address, and 112 of those is longer than
 * anybody wants to explain.
 */
void bringup_report(void);

#endif /* BRINGUP_H */
