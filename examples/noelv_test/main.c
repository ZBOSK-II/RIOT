/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief   NOEL-V port test: GPIO (LEDs + buttons) and UART
 *
 * @author  Matvii Ivashchenko
 *
 * Tests:
 *  1. UART  — print messages
 *  2. GPIO output — blink all 8 LEDs in sequence (Knight Rider effect)
 *  3. GPIO input  — read buttons, light corresponding LED
 */

#include <stdio.h>
#include <stdbool.h>
#include "board.h"
#include "clk.h"
#include "periph/gpio.h"
#include "ztimer.h"

static void delay_ms(uint32_t ms)
{
    ztimer_sleep(ZTIMER_MSEC, ms);
}

static const gpio_t leds[] = {
    LED0_PIN, LED1_PIN, LED2_PIN, LED3_PIN,
    LED4_PIN, LED5_PIN, LED6_PIN, LED7_PIN,
};
#define LED_NUMOF   (sizeof(leds) / sizeof(leds[0]))

static void led_only(int idx)
{
    for (unsigned i = 0; i < LED_NUMOF; i++) {
        if ((int)i == idx) {
            gpio_set(leds[i]);
        }
        else {
            gpio_clear(leds[i]);
        }
    }
}

static void knight_rider(int n_rounds)
{
    for (int r = 0; r < n_rounds; r++) {
        for (int i = 0; i < (int)LED_NUMOF; i++) {
            led_only(i);
            delay_ms(80);
        }
        for (int i = (int)LED_NUMOF - 2; i > 0; i--) {
            led_only(i);
            delay_ms(80);
        }
    }
    led_only(-1); /* all off */
}
static const gpio_t sw_pins[]  = { SW0_PIN, SW1_PIN, SW2_PIN };
static const gpio_t sw_leds[]  = { LED0_PIN, LED1_PIN, LED2_PIN };
#define SW_NUMOF   (sizeof(sw_pins) / sizeof(sw_pins[0]))

static const gpio_t btn_pins[] = { BTN1_PIN, BTN2_PIN, BTN3_PIN };
static const gpio_t btn_leds[] = { LED5_PIN, LED6_PIN, LED7_PIN };
#define BTN_NUMOF  (sizeof(btn_pins) / sizeof(btn_pins[0]))

static void interactive_mode(void)
{
    bool btn_prev[BTN_NUMOF]  = { false };
    bool led_state[BTN_NUMOF] = { false };

    led_only(-1);
    for (unsigned i = 0; i < BTN_NUMOF; i++) {
        gpio_clear(btn_leds[i]);
    }

    while (1) {
        for (unsigned i = 0; i < SW_NUMOF; i++) {
            if (gpio_read(sw_pins[i])) {
                gpio_set(sw_leds[i]);
            }
            else {
                gpio_clear(sw_leds[i]);
            }
        }

        for (unsigned i = 0; i < BTN_NUMOF; i++) {
            bool now = gpio_read(btn_pins[i]);
            if (now && !btn_prev[i]) {
                led_state[i] = !led_state[i];
                if (led_state[i]) {
                    gpio_set(btn_leds[i]);
                }
                else {
                    gpio_clear(btn_leds[i]);
                }
            }
            btn_prev[i] = now;
        }

        ztimer_sleep(ZTIMER_MSEC, 30);
    }
}

int main(void)
{
    puts("\r\n=== NoeIV RIOT port test ===\r\n");

    puts("[TEST 1] UART: OK (you can read this)");
    printf("  coreclk = %lu Hz\r\n", (unsigned long)coreclk());

    puts("[TEST 2] GPIO output: Knight Rider on LD0..LD7");
    knight_rider(10);
    puts("  done");

    puts("[TEST 3] Clock frequency: LED0 blinks 30x at 1 Hz (measure 30 s)");
    printf("  CLOCK_CORECLOCK = %lu Hz\r\n", (unsigned long)coreclk());
    for (int i = 0; i < 30; i++) {
        gpio_set(LED0_PIN);
        ztimer_sleep(ZTIMER_MSEC, 500);
        gpio_clear(LED0_PIN);
        ztimer_sleep(ZTIMER_MSEC, 500);
    }
    puts("  done — 30 blinks should take exactly 30 s");

    puts("\r\n[INTERACTIVE] switches latch LEDs, buttons toggle-latch LEDs");
    puts("  SW0->LD0  SW1->LD1  SW2->LD2   (held while switch is up)");
    puts("  BTN1->LD5 BTN2->LD6 BTN3->LD7  (press to flip & hold)");
    puts("  runs until board reset. (SW3 left free — it is the UART mux select)");
    interactive_mode();

    return 0;
}
