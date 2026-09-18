/**
 * @file main.c
 * @brief hmi-edge-node, the composition root. Initialises the drivers and runs
 *        the poll loop, wiring the HAL (board / uart / adc) to the pure
 *        application logic (telemetry / temperature). No hardware detail
 *        lives here.
 *
 * Each button press emits, over USART2 (the ST-Link virtual COM port):
 *     `equipment/0/state,on|off\n`
 *     `temp,<degrees>\n`
 * which the industrial-hmi SerialBackend parses. See README.md.
 */

#include "adc.h"
#include "board.h"
#include "core.h"
#include "telemetry.h"
#include "temperature.h"
#include "uart.h"

#include <stdint.h>

/// Crude debounce after a press, at the 4 MHz reset clock.
#define BUTTON_DEBOUNCE_LOOPS 120000UL

/**
 * @brief Called by the startup code before `.data` is copied and before main.
 *
 * Runs earlier than everything else, so it must not touch initialised globals.
 * It only delegates to the core HAL. The clock is left alone on purpose: this
 * firmware runs on the MSI reset clock.
 */
void SystemInit(void) {
    core_enable_fpu();
}

static void busy_wait(volatile uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

/// Read once at start-up: these are burned at the factory and never change.
static temperature_cal_t g_cal;

/**
 * The adapter telemetry is given: driver reads on one side, pure conversion
 * on the other. This is the only place the two halves meet, which is why it
 * lives in the composition root and not in either module.
 */
static int32_t read_die_temperature(void) {
    /* The labelling happens here, not in `adc`. Having the driver return these
       types would read better, but it would make the HAL depend on application
       code and invert the layering. This is the composition root, where the
       driver and the maths already meet, so it is where they get their names. */
    const ts_counts_t      ts   = { adc_temp_read() };
    const vrefint_counts_t vref = { adc_vref_read() };
    return temperature_deci_celsius(&g_cal, ts, vref);
}

int main(void) {
    board_init();
    uart_init();
    adc_temp_init();

    g_cal.ts_cal1     = adc_ts_cal1();
    g_cal.ts_cal2     = adc_ts_cal2();
    g_cal.vrefint_cal = adc_vrefint_cal();

    telemetry_state_t state;
    telemetry_init(&state);

    for (;;) {
        uint32_t pressed = board_button_pressed();
        if (telemetry_update(&state, pressed, read_die_temperature,
                             uart_send_string)) {
            board_led_set(state.equipment_on);
            busy_wait(BUTTON_DEBOUNCE_LOOPS);
        }
    }
}
