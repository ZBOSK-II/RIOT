/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_zedboard_noelv
 * @{
 *
 * @file
 * @brief       Peripheral configuration for the Gaisler NOEL-V ZedBoard
 *
 * Peripheral addresses are taken from: grmon4> info sys
 *
 * @author      Matvii Ivashchenko
 */

#include "kernel_defines.h"
#include "periph_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Timer configuration
 *
 * One timer via RISC-V ACLINT (CLINT).
 * CLINT base address is defined in cpu_conf.h (0xe0000000).
 * @{
 */
#define TIMER_NUMOF         (1)
/** @} */

/**
 * @name    GPIO configuration
 *
 * grmon: gpio0  APB: ff983000 - ff983100
 * @{
 */
#define GPIO0_BASE_ADDR     (0xff983000UL)  /**< GRGPIO base address */
/** @} */

/**
 * @name    UART configuration
 *
 * grmon: uart0  APB: ff900000 - ff900100  IRQ: 1
 *
 * SW3 = 0: UART0 routed to RIOT application
 * SW3 = 1: UART0 routed to debug (GRMON)
 * @{
 */
static const uart_conf_t uart_config[] = {
    {
        .addr   = 0xff900000UL, /* APBUART0 base address */
        .irq    = 1,            /* PLIC interrupt source 1 */
    },
};

#define UART_NUMOF          ARRAY_SIZE(uart_config)
/** @} */

/**
 * @name    GRETH Ethernet configuration
 *
 * grmon: greth0  APB: ff984000 - ff984100  IRQ: 5
 * @{
 */
#define GRETH_PARAM_BASE    (0xff984000UL)  /**< GRETH base address */
#define GRETH_PARAM_IRQ     (5U)            /**< GRETH interrupt line */
/** @} */


#ifdef __cplusplus
}
#endif

/** @} */
