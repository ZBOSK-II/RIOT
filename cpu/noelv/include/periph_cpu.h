/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     cpu_noelv
 * @{
 *
 * @file
 * @brief       CPU specific peripheral configuration for NOEL-V
 *
 * @author      Matvii Ivashchenko
 */

#include <stdint.h>
#include "periph_cpu_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   GPIO pin mapping: port is always 0 for GRLIB GPIO0
 */
#define GPIO_PIN(port, pin)     ((gpio_t)((port << 5) | (pin)))

/**
 * @brief   Structure for UART configuration
 *
 * GRLIB APBUART does not need explicit RX/TX GPIO pins since
 * they are hardwired in the FPGA design.
 */
typedef struct {
    uint32_t addr;      /**< APBUART base address on APB bus */
    uint32_t irq;       /**< PLIC interrupt source number */
} uart_conf_t;

/**
 * @brief   UART interrupt priority
 */
#define UART_ISR_PRIO       (1)
#define PERIPH_TIMER_PROVIDES_SET   /**< timer_set() is provided by the driver */
#ifdef __cplusplus
}
#endif

/** @} */
