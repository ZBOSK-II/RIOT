/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_arty_a7_noelv
 * @{
 *
 * @file
 * @brief       Board specific definitions for the Gaisler NOEL-V Arty A7
 *
 * @author      Matvii Ivashchenko
 */

#include "cpu.h"
#include "periph_conf.h"
#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif


#define CONFIG_ZTIMER_USEC_BASE_FREQ (CLOCK_CORECLOCK / 2) /**< ztimer base = mtime */

#define LED0_PIN            GPIO_PIN(0, 16) /**< LD4 → gpio_o[16] */
#define LED1_PIN            GPIO_PIN(0, 17) /**< LD5 → gpio_o[17] */
#define LED2_PIN            GPIO_PIN(0, 18) /**< LD6 → gpio_o[18] */
#define LED3_PIN            GPIO_PIN(0, 19) /**< LD7 → gpio_o[19] */

#define LED0_ON             gpio_set(LED0_PIN)
#define LED0_OFF            gpio_clear(LED0_PIN)
#define LED0_TOGGLE         gpio_toggle(LED0_PIN)

#define LED1_ON             gpio_set(LED1_PIN)
#define LED1_OFF            gpio_clear(LED1_PIN)
#define LED1_TOGGLE         gpio_toggle(LED1_PIN)


#define BTN1_PIN            GPIO_PIN(0, 5) /**< BTN1  → gpio_i[5] */
#define BTN2_PIN            GPIO_PIN(0, 6) /**< BTN2  → gpio_i[6] */
#define BTN3_PIN            GPIO_PIN(0, 7) /**< BTN3  → gpio_i[7] */
#define BTN4_PIN            GPIO_PIN(0, 0) /**< SW0   → gpio_i[0] */
#define BTN5_PIN            GPIO_PIN(0, 1) /**< SW1   → gpio_i[1] */
#define BTN6_PIN            GPIO_PIN(0, 2) /**< SW2   → gpio_i[2] */
#define BTN7_PIN            GPIO_PIN(0, 3) /**< SW3   → gpio_i[3] */

#define BTN1_MODE           GPIO_IN
#define BTN2_MODE           GPIO_IN
#define BTN3_MODE           GPIO_IN
#define BTN4_MODE           GPIO_IN
#define BTN5_MODE           GPIO_IN
#define BTN6_MODE           GPIO_IN
#define BTN7_MODE           GPIO_IN

#define SW0_PIN             GPIO_PIN(0, 0)  /**< SW0 -> gpio_i[0] */
#define SW1_PIN             GPIO_PIN(0, 1)  /**< SW1 -> gpio_i[1] */
#define SW2_PIN             GPIO_PIN(0, 2)  /**< SW2 -> gpio_i[2] */
#define SW3_PIN             GPIO_PIN(0, 3)  /**< SW3 -> gpio_i[3] (UART/DSU mux) */

#ifdef __cplusplus
}
#endif

