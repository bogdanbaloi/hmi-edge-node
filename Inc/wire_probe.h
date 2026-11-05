#ifndef WIRE_PROBE_H
#define WIRE_PROBE_H

#include <stdint.h>

/**
 * @file wire_probe.h
 * @brief Application logic: ask whether a wire is physically there, which is
 *        a question no protocol can answer.
 *
 * A bus protocol only speaks once the wiring already works. When nothing
 * answers, every cause looks the same from inside the protocol: a dead
 * device, a device at another address, a device in the wrong mode, or a
 * jumper in the hole next door. On 2026-11-05 that one word, `absent`, hid a
 * data line sitting on PC9 instead of PB9 for most of an afternoon, while
 * photographs of the board were read and misread three times.
 *
 * The trick this module is built on is a comparison of two resistors. A
 * sensor module carries its own pull-ups to its supply, around 4.7k. The chip
 * has internal pulls of its own, around 40k, roughly ten times weaker. So
 * read a pin twice, once left alone and once while the chip pulls it DOWN:
 *
 *   still high -> something outside is holding it up, so the wire reaches
 *                 this pin AND the thing at the far end has power
 *   now low    -> nothing outside. The wire goes elsewhere, or is broken, or
 *                 the module is not powered
 *
 * Pure logic, no registers. The pin read is injected, so a host test drives
 * it from a table. Sibling of adxl_report.c, same shape and the same reasons.
 */

/// How the chip pulls a pin while it is being read.
typedef enum {
    WIRE_PULL_NONE = 0,  ///< Left alone, so the pin floats or follows outside.
    WIRE_PULL_DOWN       ///< The chip's own weak pull, for the comparison.
} wire_pull_t;

/**
 * Read one pin as a plain input with the given pull, and answer 1 for high.
 *
 * On target this writes MODER and PUPDR and reads IDR, then waits long
 * enough for the line to settle. In a host test it is a lookup. `port` is
 * whatever the caller's adapter uses to tell ports apart.
 */
typedef uint32_t (*wire_read_t)(void *ctx, uint8_t port, uint8_t pin,
                                wire_pull_t pull);

/// Receives one complete line to transmit. On target: uart_send_string.
typedef void (*wire_sink_t)(const char *line);

/// The one thing this module needs from the board.
typedef struct {
    void *ctx;
    wire_read_t read;
} wire_bus_t;

/// One port to sweep, and the pins in it that must be left alone.
typedef struct {
    char letter;    ///< How the port is named on the wire, such as 'B'.
    uint8_t port;   ///< What the read function is given.
    /// Pins that already have a job. Reading them as plain inputs would cut
    /// the UART this report travels on, or the debug pins the programmer
    /// uses, so they are never touched.
    uint16_t keep;
} wire_port_t;

/// Pins in one port, so a sweep knows where to stop.
#define WIRE_PINS_PER_PORT 16U

/// Longest line this writes is `held,B15` plus the terminator.
#define WIRE_LINE_MAX 12U

/// What the two readings of one pin mean together.
typedef enum {
    WIRE_LOOSE = 0,  ///< Nothing outside holds this pin up.
    WIRE_HELD        ///< Something outside wins against the chip's own pull.
} wire_state_t;

/// Read one pin twice and say whether anything outside is holding it.
wire_state_t wire_probe_pin(const wire_bus_t *bus, uint8_t port, uint8_t pin);

/**
 * @brief Report one named line as two digits: left alone, then pulled down.
 *
 * `11` is a healthy wire with something powered behind it. `10` is a pin with
 * nothing on it, whatever the first digit suggested: a floating input reads
 * whatever charge is left on it, which is why one reading is not enough.
 */
void wire_probe_report(const wire_bus_t *bus, const char *id, uint8_t port,
                       uint8_t pin, wire_sink_t sink);

/**
 * @brief Name every pin that something outside is holding up.
 *
 * Turns "which hole is that jumper in" from a question about a photograph
 * into a measurement. Writes `held,<letter><pin>` for each, then `held,end`
 * so a reader knows the sweep finished rather than stopped.
 *
 * @return how many pins answered.
 */
uint8_t wire_probe_sweep(const wire_bus_t *bus, const wire_port_t *ports,
                         uint8_t port_count, wire_sink_t sink);

#endif /* WIRE_PROBE_H */
