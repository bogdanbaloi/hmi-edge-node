/**
 * @file main.c
 * @brief hmi-edge-node -- composition root. Initialises the drivers and runs
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
#include "telemetry.h"
#include "temperature.h"
#include "uart.h"

#include <stdint.h>

/// Crude debounce after a press, at the 4 MHz reset clock.
#define BUTTON_DEBOUNCE_LOOPS 120000UL

/// Startup calls SystemInit before main; we keep the reset clock, so no-op.
void SystemInit(void) {
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
    const uint32_t ts_raw      = adc_temp_read();
    const uint32_t vrefint_raw = adc_vref_read();
    return temperature_deci_celsius(&g_cal, ts_raw, vrefint_raw);
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
