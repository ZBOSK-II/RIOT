/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @brief   NoeIV port test: GPIO (LEDs + buttons) and UART
 *
 * Tests:
 *  1. UART  — print messages
 *  2. GPIO output — blink all 8 LEDs in sequence (Knight Rider effect)
 *  3. GPIO input  — read buttons, light corresponding LED
 */

#include <stdio.h>
#include "board.h"
#include "clk.h"
#include "periph/gpio.h"

static void delay_ms(uint32_t ms)
{
    uint32_t loops = (coreclk() / 20) / 1000 * ms;
    for (volatile uint32_t i = 0; i < loops; i++) {}
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

int main(void)
{
    puts("\r\n=== NoeIV RIOT port test ===\r\n");

    puts("[TEST 1] UART: OK (you can read this)");
    printf("  coreclk = %lu Hz\r\n", (unsigned long)coreclk());

    puts("[TEST 2] GPIO output: Knight Rider on LD0..LD7");
    knight_rider(10);
    puts("  done");

    puts("[TEST 3] GPIO input: press BTND/BTNL/BTNR (10 seconds)");
    puts("  BTN1=BTND -> LD0,  BTN2=BTNL -> LD1,  BTN3=BTNR -> LD2");

    for (int i = 0; i < 2000; i++) {           
        bool b1 = gpio_read(BTN1_PIN);
        bool b2 = gpio_read(BTN2_PIN);
        bool b3 = gpio_read(BTN3_PIN);
        bool b4 = gpio_read(BTN4_PIN);
        bool b5 = gpio_read(BTN5_PIN);
        bool b6 = gpio_read(BTN6_PIN);
        bool b7 = gpio_read(BTN7_PIN);

        if (b1) { gpio_set(LED0_PIN); } else { gpio_clear(LED0_PIN); }
        if (b2) { gpio_set(LED1_PIN); } else { gpio_clear(LED1_PIN); }
        if (b3) { gpio_set(LED2_PIN); } else { gpio_clear(LED2_PIN); }
        if (b4) { gpio_set(LED3_PIN); } else { gpio_clear(LED3_PIN); }
        if (b5) { gpio_set(LED4_PIN); } else { gpio_clear(LED4_PIN); }
        if (b6) { gpio_set(LED5_PIN); } else { gpio_clear(LED5_PIN); }
        if (b7) { gpio_set(LED6_PIN); } else { gpio_clear(LED6_PIN); }

        delay_ms(50);
    }

    led_only(-1);
    puts("  done");

    puts("\r\n=== All tests complete ===");
    return 0;
}
