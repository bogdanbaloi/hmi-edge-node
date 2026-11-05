/**
 * @file wire_probe.c
 * @brief Is the wire there? Two readings of one pin, and a comparison of two
 *        resistors.
 */

#include "wire_probe.h"

/* Behind the same switch as the diagnostic that uses it, so a shipping
   image carries none of it. Measured: 432 bytes of unreachable code when
   it was linked unconditionally. The host test defines the switch, which
   is why the tests still cover every line of it. */
#ifdef BRINGUP_DIAGNOSTIC

#include <stddef.h>

/* The digits of the two-reading report, in the order they are written. */
#define DIGIT_ALONE_AT     0U
#define DIGIT_PULLED_AT    1U

#define DECIMAL_BASE 10

static char *append_text(char *at, const char *text) {
    while (*text != '\0') {
        *at++ = *text++;
    }
    return at;
}

/**
 * A fourth small decimal formatter in this repository, and deliberate for the
 * same reason vib_telemetry.c carries the second: telemetry.c's is pinned by
 * a contract test with the host, and widening it so a diagnostic can print a
 * pin number would put this module's needs onto the one the other repository
 * depends on. The numbers here never exceed two digits.
 */
static char *append_pin(char *at, uint8_t pin) {
    if (pin >= DECIMAL_BASE) {
        *at++ = (char)('0' + (pin / DECIMAL_BASE));
    }
    *at++ = (char)('0' + (pin % DECIMAL_BASE));
    return at;
}

static void send(wire_sink_t sink, const char *line) {
    if (sink != NULL) {
        sink(line);
    }
}

wire_state_t wire_probe_pin(const wire_bus_t *bus, uint8_t port, uint8_t pin) {
    if (bus == NULL || bus->read == NULL) {
        return WIRE_LOOSE;
    }
    /* The reading WITHOUT a pull is not the answer and is never used for the
       verdict. A pin left alone reads whatever charge happens to sit on it,
       which is why it so often reads high on a wire that goes nowhere. It is
       reported because the pair of digits tells a person more than the
       verdict does, and it is read first because reading it second would
       show the pull-down's leftover rather than the line's own state. */
    (void)bus->read(bus->ctx, port, pin, WIRE_PULL_NONE);
    return (bus->read(bus->ctx, port, pin, WIRE_PULL_DOWN) != 0U)
               ? WIRE_HELD
               : WIRE_LOOSE;
}

void wire_probe_report(const wire_bus_t *bus, const char *id, uint8_t port,
                       uint8_t pin, wire_sink_t sink) {
    if (bus == NULL || bus->read == NULL || id == NULL) {
        return;
    }
    const uint32_t alone = bus->read(bus->ctx, port, pin, WIRE_PULL_NONE);
    const uint32_t pulled = bus->read(bus->ctx, port, pin, WIRE_PULL_DOWN);

    char line[WIRE_LINE_MAX];
    char *at = append_text(line, id);
    *at++ = ',';
    at[DIGIT_ALONE_AT] = (char)('0' + (alone != 0U ? 1U : 0U));
    at[DIGIT_PULLED_AT] = (char)('0' + (pulled != 0U ? 1U : 0U));
    at += 2;
    *at++ = '\n';
    *at = '\0';
    send(sink, line);
}

uint8_t wire_probe_sweep(const wire_bus_t *bus, const wire_port_t *ports,
                         uint8_t port_count, wire_sink_t sink) {
    if (bus == NULL || bus->read == NULL || ports == NULL) {
        return 0U;
    }
    uint8_t found = 0U;
    for (uint8_t which = 0U; which < port_count; which++) {
        const wire_port_t *port = &ports[which];
        for (uint8_t pin = 0U; pin < WIRE_PINS_PER_PORT; pin++) {
            if ((port->keep & (uint16_t)(1U << pin)) != 0U) {
                continue;
            }
            if (wire_probe_pin(bus, port->port, pin) != WIRE_HELD) {
                continue;
            }
            char line[WIRE_LINE_MAX];
            char *at = append_text(line, "held");
            *at++ = ',';
            *at++ = port->letter;
            at = append_pin(at, pin);
            *at++ = '\n';
            *at = '\0';
            send(sink, line);
            found++;
        }
    }
    /* Said even when nothing answered, because a sweep that found nothing and
       a sweep that never ran look the same on a wire otherwise. */
    send(sink, "held,end\n");
    return found;
}

#endif /* BRINGUP_DIAGNOSTIC */
