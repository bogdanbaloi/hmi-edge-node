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
 *
 * On the same wire, the other way, the industrial-hmi update agent sends
 * binary frames (uart-flash-v1.md). They arrive through the UART receive
 * interrupt, go through the frame parser into the update state machine, and
 * its answers go back out between the telemetry lines. During an update
 * session telemetry stays quiet, as the spec requires.
 */

#include "adc.h"
#include "board.h"
#include "byte_ring.h"
#include "core.h"
#include "crc32.h"
#include "flash.h"
#include "ota_port.h"
#include "ota_frame_parser.h"
#include "ota_update.h"
#include "telemetry.h"
#include "temperature.h"
#include "uart.h"

#include <stdint.h>


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

/* The receive queue must hold a whole frame: the host sends the next one only
   after the answer, so one frame is the most that can pile up unread. */
_Static_assert(BYTE_RING_CAPACITY >= OTA_FRAME_MAX_SIZE,
               "the UART receive queue must hold the largest frame");

static ota_frame_parser_t g_parser;
static ota_update_t g_update;

/**
 * Hand every byte received since the last pass to the parser, which hands
 * whole frames to the update state machine, then let it end a session that
 * went silent. Never blocks: an empty queue returns at once.
 */
static void ota_poll(void) {
    uint8_t byte = 0U;
    while (uart_read_byte(&byte) == UART_BYTE_READ) {
        ota_frame_parser_feed(&g_parser, byte, ota_update_on_frame, &g_update);
    }
    ota_update_tick(&g_update);
}

int main(void) {
    board_init();
    flash_init();
    crc32_init();
    ota_frame_parser_init(&g_parser);
    uart_init();
    adc_temp_init();

    g_cal.ts_cal1     = adc_ts_cal1();
    g_cal.ts_cal2     = adc_ts_cal2();
    g_cal.vrefint_cal = adc_vrefint_cal();

    core_tick_init();

    telemetry_state_t state;
    telemetry_init(&state);
    ota_update_init(&g_update, ota_port());

    for (;;) {
        /* Nothing here stalls. Debouncing is a decision telemetry makes from
           the clock, not a delay this loop sits through, so the processor
           stays free for whatever gets added next. */
        ota_poll();
        if (ota_update_in_session(&g_update)) {
            continue;  /* section 5: no telemetry during an update */
        }
        const uint32_t pressed = board_button_pressed();
        if (telemetry_update(&state, pressed, core_millis(),
                             read_die_temperature, uart_send_string)) {
            board_led_set(state.equipment_on);
        }
    }
}
