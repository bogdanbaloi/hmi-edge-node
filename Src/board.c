/**
 * @file board.c
 * @brief Board support for the Nucleo-L476RG. The only place GPIOA/GPIOC
 *        registers are touched.
 */

#include "board.h"
#include "registers.h"

void board_init(void) {
    /* Port clocks: GPIOA (TX + LED) and GPIOC (button). */
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN;

    /* PA2 -> alternate function AF7 (USART2_TX).
       AFRL FIRST, MODER second. The other order put the pin into alternate
       function mode while AFRL still selected AF0, which is not the USART, for
       the few instructions in between. SUSPECTED, not yet proven, to be the
       single 0xFF every reset put on the line (15 out of 15, 2026-09-22).
       ST's own HAL (HAL_GPIO_Init) writes AFR before MODER. */
    GPIOA_AFRL  &= ~(0xFUL << (4U * PIN_USART2_TX));
    GPIOA_AFRL  |=  (7UL   << (4U * PIN_USART2_TX));
    GPIOA_MODER &= ~(3UL << (2U * PIN_USART2_TX));
    GPIOA_MODER |=  (2UL << (2U * PIN_USART2_TX));

    /* PA5 -> general-purpose output (LED LD2). */
    GPIOA_MODER &= ~(3UL << (2U * PIN_LED));
    GPIOA_MODER |=  (1UL << (2U * PIN_LED));

    /* PC13 -> input with pull-up (button). GPIOC resets to analog, so set
     * input explicitly, then enable the internal pull-up. */
    GPIOC_MODER &= ~(3UL << (2U * PIN_BUTTON));
    GPIOC_PUPDR &= ~(3UL << (2U * PIN_BUTTON));
    GPIOC_PUPDR |=  (1UL << (2U * PIN_BUTTON));
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
