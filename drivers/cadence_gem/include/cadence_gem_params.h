/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     drivers_cadence_gem
 * @{
 *
 * @file
 * @brief       Default configuration for the Cadence GEM Ethernet driver
 *
 * Board-specific values are expected in periph_conf.h via GEM_PARAM_BASE,
 * GEM_PARAM_IRQ, and GEM_PARAM_MAC.
 *
 * @author      Matvii Ivashchenko
 */

#include "cadence_gem.h"
#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Default parameters
 *
 * GEM_PARAM_BASE: NOEL-V alias address of PS GEM0.
 *   VHDL translates 0x6000_B000 (NOEL-V) → 0xE000_B000 (PS GEM0 registers).
 *
 * GEM_PARAM_IRQ: set to 0 because Phase 1 uses a polling thread, not a
 *   hardware interrupt routed from PS GIC to NOEL-V PLIC.
 * @{
 */
#ifndef GEM_PARAM_BASE
#define GEM_PARAM_BASE  (0x6000B000UL)
#endif

#ifndef GEM_PARAM_IRQ
#define GEM_PARAM_IRQ   (0U)
#endif

#ifndef GEM_PARAM_MAC
#define GEM_PARAM_MAC   { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 }
#endif

#ifndef GEM_PARAMS
#define GEM_PARAMS      { .base_addr = GEM_PARAM_BASE, \
                          .irq       = GEM_PARAM_IRQ,  \
                          .mac       = GEM_PARAM_MAC }
#endif
/** @} */

/**
 * @brief   GEM device configuration table
 */
static const gem_params_t gem_params[] = {
    GEM_PARAMS
};

#ifdef __cplusplus
}
#endif

/** @} */
