#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/**
 * @file board.h
 * @brief Board support (BSP) for the Nucleo-L476RG: the GPIO port clocks and
 *        the specific pins this project uses. Board-specific, so a different
 *        board changes only this file.
 *
 * Pin map:
 *   PA2  -> USART2_TX (AF7), wired to the ST-Link virtual COM port
 *   PA5  -> LED LD2 (output)
 *   PC13 -> USER button B1 (input, pull-up; reads low when pressed)
 */

/// Enable the GPIO port clocks and configure the board pins.
void board_init(void);

/// Drive the on-board LED (LD2): non-zero = on.
void board_led_set(uint32_t on);

/// Non-zero while the USER button (B1) is pressed.
uint32_t board_button_pressed(void);

#endif /* BOARD_H */
