/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    drivers_cadence_gem Cadence GEM Ethernet MAC driver
 * @ingroup     drivers_netdev
 * @brief       Driver for the Cadence GEM MAC (Zynq-7000 PS GEM0)
 *
 * ## Overview
 *
 * Cadence GEM (also known as MACB) is the Gigabit Ethernet MAC built into
 * the Zynq-7000 Processing System (PS). On ZedBoard, GEM0 is connected to
 * a Marvell 88E1518 PHY via PS MIO 16–27 (RGMII).
 *
 * This driver accesses GEM0 registers from the NOEL-V RISC-V soft-core (PL)
 * via the S_AXI_GP0 path. A VHDL address translation maps NOEL-V physical
 * address 0x6000_B000 → PS address 0xE000_B000 (GEM0 register base).
 *
 * ## DMA address translation
 *
 * NOEL-V physical addresses are mapped to PS DDR physical addresses by the
 * hardware: PS_DDR_PHYS = (NOELV_PHYS & 0x0FFFFFFF) | 0x10000000.
 * GEM DMA descriptors must contain PS DDR addresses (use GEM_TO_PS_PHYS).
 *
 * ## RX model (Phase 1)
 *
 * A background polling thread checks the RX descriptor ring every 1 ms and
 * signals NETDEV_EVENT_ISR to the netif thread when a frame arrives.
 * Hardware interrupt routing (PS→PL via EMIO) is not required.
 *
 * @{
 *
 * @file
 * @brief       Interface definitions for the Cadence GEM Ethernet driver
 *
 * @author      Matvii Ivashchenko
 */

#include <stdint.h>
#include <stdbool.h>

#include "net/netdev.h"
#include "net/netdev/eth.h"
#include "cadence_gem_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Driver configuration constants
 * @{
 */

/** Number of RX descriptors (power of 2). */
#ifndef CONFIG_GEM_RX_DESC_NUM
#define CONFIG_GEM_RX_DESC_NUM  (8U)
#endif

/** Number of TX descriptors (power of 2). */
#ifndef CONFIG_GEM_TX_DESC_NUM
#define CONFIG_GEM_TX_DESC_NUM  (4U)
#endif

/** Maximum Ethernet frame size excluding FCS. */
#define GEM_MAX_FRAME_LEN       (1518U)

/** Buffer size aligned to 64-byte (DMA cache-line) boundary. */
#define GEM_BUF_SIZE            (1536U)

/** PHY auto-negotiate poll iterations before timeout. */
#define GEM_ANEG_TIMEOUT        (100000U)

/** MDIO operation timeout iterations. */
#define GEM_MDIO_TIMEOUT        (200000U)

/**
 * @brief   Translate NOEL-V physical address to PS DDR physical address
 *          for use in GEM DMA descriptors.
 *
 * Hardware mapping in noelvmp.vhd: PS_ADDR = 0x10000000 | (NOELV_ADDR & 0x0FFFFFFF).
 * NOEL-V RAM starts at 0x00800000 → PS DDR 0x10800000.
 */
#define GEM_TO_PS_PHYS(a) \
    ((uint32_t)(((uint32_t)(uintptr_t)(a) & 0x0FFFFFFFu) | 0x10000000u))

/** @} */

/**
 * @brief   Cadence GEM driver parameters (board-level configuration)
 */
typedef struct {
    uint32_t base_addr; /**< GEM register base in NOEL-V alias space (e.g. 0x6000B000) */
    unsigned irq;       /**< PLIC interrupt source (0 = polling, no IRQ wiring needed) */
    uint8_t  mac[6];    /**< Ethernet MAC address */
} gem_params_t;

/**
 * @brief   Cadence GEM driver device descriptor
 *
 * The @p netdev member must remain first so the driver can be cast between
 * `gem_t *` and `netdev_t *`.
 */
typedef struct {
    netdev_t netdev;             /**< netdev base — MUST be first */
    const gem_params_t *params;  /**< Board-level parameters */
    gem_rx_desc_t *rx_desc;      /**< RX descriptor ring (file-scope static) */
    gem_tx_desc_t *tx_desc;      /**< TX descriptor ring (file-scope static) */
    uint8_t *tx_buf;             /**< Flat TX frame buffer */
    unsigned rx_idx;             /**< Next RX descriptor to check */
    unsigned tx_idx;             /**< Next TX descriptor to use */
    unsigned phy_addr;           /**< PHY MDIO address (detected at init) */
} gem_t;

/**
 * @brief   Set up the GEM device descriptor
 *
 * Call before netdev->init(). Wires the netdev driver pointer and stores
 * board parameters. Does not touch hardware.
 *
 * @param[out]  dev     Driver state to initialise
 * @param[in]   params  Board-level configuration
 * @param[in]   index   Instance index for netdev_register
 */
void gem_setup(gem_t *dev, const gem_params_t *params, uint8_t index);

#ifdef __cplusplus
}
#endif

/** @} */
