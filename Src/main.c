/**
 * @file main.c
 * @brief hmi-edge-node -- composition root. Initialises the drivers and runs
 *        the poll loop, wiring the HAL (board / uart / adc) to the pure
 *        application logic (telemetry). No hardware detail lives here.
 *
 * Each button press emits, over USART2 (the ST-Link virtual COM port):
 *     equipment/0/state,on|off\n
 *     temp,<raw>\n
 * which the industrial-hmi SerialBackend parses. See README.md.
 */

#include "adc.h"
#include "board.h"
#include "telemetry.h"
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

int main(void) {
    board_init();
    uart_init();
    adc_temp_init();

    telemetry_state_t state;
    telemetry_init(&state);

    for (;;) {
        uint32_t pressed = board_button_pressed();
        if (telemetry_update(&state, pressed, adc_temp_read, uart_send_string)) {
            board_led_set(state.equipment_on);
            busy_wait(BUTTON_DEBOUNCE_LOOPS);
        }
    }
}
