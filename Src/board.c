/**
 * @file board.c
 * @brief Board support for the Nucleo-L476RG. The only place GPIOA/GPIOC
 *        registers are touched.
 */

#include "board.h"
#include "registers.h"

void board_init(void) {
    /* Port clocks: GPIOA (TX, RX, LED) and GPIOC (button). */
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN;

    /* PA2 -> alternate function AF7 (USART2_TX), PA3 -> AF7 (USART2_RX).
       AFRL FIRST, MODER second. The other order put the pin into alternate
       function mode while AFRL still selected AF0, which is not the USART, for
       the few instructions in between, and every reset put exactly one 0xFF on
       the line: 46 resets out of 46. With this order, 0 out of at least 6,
       measured on the board on 2026-09-22 with nothing else changed. The host
       used to receive that 0xFF glued to the front of the first frame after a
       reset. ST's own HAL (HAL_GPIO_Init) writes AFR before MODER.
       RX follows the same order. On RX the wrong order would not put a byte
       on the line, since the pin is an input either way, but one rule for
       both pins is one rule nobody has to remember the exception to.
       PA3 is USART2_RX on AF7: datasheet DS10198, Table 17, page 92.
       No pull-up on RX: the ST-Link drives the line, as in ST's own setup. */
    GPIOA_AFRL  &= ~(GPIO_AF_MASK(PIN_USART2_TX) | GPIO_AF_MASK(PIN_USART2_RX));
    GPIOA_AFRL  |=  GPIO_AF(PIN_USART2_TX, GPIO_AF7_USART) |
                    GPIO_AF(PIN_USART2_RX, GPIO_AF7_USART);
    GPIOA_MODER &= ~(GPIO_MODE_MASK(PIN_USART2_TX) |
                     GPIO_MODE_MASK(PIN_USART2_RX));
    GPIOA_MODER |=  GPIO_MODE(PIN_USART2_TX, GPIO_MODE_ALTERNATE) |
                    GPIO_MODE(PIN_USART2_RX, GPIO_MODE_ALTERNATE);

    /* PA5 -> general-purpose output (LED LD2). */
    GPIOA_MODER &= ~GPIO_MODE_MASK(PIN_LED);
    GPIOA_MODER |=  GPIO_MODE(PIN_LED, GPIO_MODE_OUTPUT);

    /* PC13 -> input with pull-up (button). GPIOC resets to analog, so set
     * input explicitly (all mode bits clear), then enable the pull-up. */
    GPIOC_MODER &= ~GPIO_MODE_MASK(PIN_BUTTON);
    GPIOC_PUPDR &= ~GPIO_PULL_MASK(PIN_BUTTON);
    GPIOC_PUPDR |=  GPIO_PULL(PIN_BUTTON, GPIO_PULL_UP);
}

void board_led_set(uint32_t on) {
    if (on) {
        GPIOA_ODR |=  (1UL << PIN_LED);
    } else {
        GPIOA_ODR &= ~(1UL << PIN_LED);
    }
}

uint32_t board_button_pressed(void) {
    /* Pull-up means the pin idles high; pressed pulls it to ground (low). */
    return (GPIOC_IDR & (1UL << PIN_BUTTON)) == 0U;
}
