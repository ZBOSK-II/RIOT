/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     boards_arty_a7_noelv
 * @{
 *
 * @file
 * @brief       Board initialization for the Gaisler NOEL-V Arty A7
 *
 * @author      Matvii Ivashchenko
 * @}
 */

#include "board.h"
#include "periph/gpio.h"

void board_init(void)
{
    /*LEDs*/
    gpio_init(LED0_PIN, GPIO_OUT);
    gpio_init(LED1_PIN, GPIO_OUT);
    gpio_init(LED2_PIN, GPIO_OUT);
    gpio_init(LED3_PIN, GPIO_OUT);

    /*buttons */
    gpio_init(BTN1_PIN, GPIO_IN);
    gpio_init(BTN2_PIN, GPIO_IN);
    gpio_init(BTN3_PIN, GPIO_IN);
    gpio_init(BTN4_PIN, GPIO_IN);
    gpio_init(BTN5_PIN, GPIO_IN);
    gpio_init(BTN6_PIN, GPIO_IN);
    gpio_init(BTN7_PIN, GPIO_IN);
}
