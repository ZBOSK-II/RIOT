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
 * @brief       Register definitions for the Cadence GEM Ethernet MAC
 *              (Zynq-7000 PS GEM0/GEM1, Zynq TRM UG585)
 *
 * @author      Matvii Ivashchenko
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Cadence GEM register map (MMIO, all 32-bit)
 *
 * Access via GEM0 base address (0x6000B000 in NOEL-V alias space
 * → 0xE000B000 in PS physical space).
 */
typedef struct {
    volatile uint32_t net_ctrl;     /**< 0x000 Network Control */
    volatile uint32_t net_cfg;      /**< 0x004 Network Configuration */
    volatile uint32_t net_status;   /**< 0x008 Network Status (read-only) */
    volatile uint32_t _pad0;        /**< 0x00C reserved */
    volatile uint32_t dma_cfg;      /**< 0x010 DMA Configuration */
    volatile uint32_t tx_status;    /**< 0x014 TX Status (write-1-to-clear) */
    volatile uint32_t rx_qbar;      /**< 0x018 RX Queue Base Address (PS DDR) */
    volatile uint32_t tx_qbar;      /**< 0x01C TX Queue Base Address (PS DDR) */
    volatile uint32_t rx_status;    /**< 0x020 RX Status (write-1-to-clear) */
    volatile uint32_t intr_status;  /**< 0x024 Interrupt Status (write-1-to-clear) */
    volatile uint32_t intr_en;      /**< 0x028 Interrupt Enable (write 1 to enable) */
    volatile uint32_t intr_dis;     /**< 0x02C Interrupt Disable (write 1 to disable) */
    volatile uint32_t intr_mask;    /**< 0x030 Interrupt Mask Status (read-only) */
    volatile uint32_t phy_maint;    /**< 0x034 PHY Maintenance (MDIO) */
    volatile uint32_t _pad1[20];    /**< 0x038–0x084 reserved/stats */
    volatile uint32_t spec_addr1_bot; /**< 0x088 MAC address [31:0] */
    volatile uint32_t spec_addr1_top; /**< 0x08C MAC address [47:32] */
} gem_regs_t;

/**
 * @name    Network Control register bits (0x000)
 * @{
 */
#define GEM_NWCTRL_RXEN         (1u << 2)   /**< Enable RX */
#define GEM_NWCTRL_TXEN         (1u << 3)   /**< Enable TX */
#define GEM_NWCTRL_MDIO_EN      (1u << 4)   /**< Enable MDIO management port */
#define GEM_NWCTRL_CLRSTAT      (1u << 5)   /**< Clear statistics registers */
#define GEM_NWCTRL_START_TX     (1u << 9)   /**< Start TX (write-only, self-clears) */
#define GEM_NWCTRL_HALT_TX      (1u << 10)  /**< Halt TX after current frame */
/** @} */

/**
 * @name    Network Configuration register bits (0x004)
 * @{
 */
#define GEM_NWCFG_SPEED_100     (1u << 0)   /**< 1=100 Mbps, 0=10 Mbps (ignored when GIGABIT) */
#define GEM_NWCFG_FULL_DUPLEX   (1u << 1)   /**< Full duplex */
#define GEM_NWCFG_GIGABIT       (1u << 10)  /**< Gigabit mode (1000 Mbps RGMII) */
#define GEM_NWCFG_COPY_ALL     (1u << 4)    /**< Promiscuous (copy all frames) */
#define GEM_NWCFG_NO_BCAST      (1u << 5)   /**< Discard broadcast frames */
#define GEM_NWCFG_MDC_DIV8      (0u << 18)  /**< MDC = PCLK/8  (PCLK ≤ 20 MHz) */
#define GEM_NWCFG_MDC_DIV16     (1u << 18)  /**< MDC = PCLK/16 (PCLK ≤ 40 MHz) */
#define GEM_NWCFG_MDC_DIV32     (2u << 18)  /**< MDC = PCLK/32 (PCLK ≤ 80 MHz) */
#define GEM_NWCFG_MDC_DIV48     (3u << 18)  /**< MDC = PCLK/48 (PCLK ≤ 120 MHz) */
#define GEM_NWCFG_MDC_DIV64     (4u << 18)  /**< MDC = PCLK/64 (PCLK ≤ 160 MHz) */
#define GEM_NWCFG_MDC_DIV96     (5u << 18)  /**< MDC = PCLK/96 (PCLK ≤ 240 MHz) */
#define GEM_NWCFG_MDC_DIV128    (6u << 18)  /**< MDC = PCLK/128 */
#define GEM_NWCFG_MDC_MASK      (7u << 18)  /**< MDC divider mask */
#define GEM_NWCFG_IGNORE_FCS    (1u << 26)  /**< Ignore RX FCS errors */
/** @} */

/**
 * @name    Network Status register bits (0x008, read-only)
 * @{
 */
#define GEM_NWSTAT_MDIO_IN      (1u << 0)   /**< MDIO input line level */
#define GEM_NWSTAT_PHY_IDLE     (1u << 2)   /**< PHY management (MDIO) idle */
/** @} */

/**
 * @name    DMA Configuration register bits (0x010)
 * @{
 */
#define GEM_DMACFG_BURST_INCR4  (0x04u)     /**< AHB burst length INCR4 */
#define GEM_DMACFG_BURST_INCR8  (0x08u)     /**< AHB burst length INCR8 */
#define GEM_DMACFG_BURST_INCR16 (0x10u)     /**< AHB burst length INCR16 */
#define GEM_DMACFG_RXBUF_SHIFT  (16u)       /**< RX buffer size field shift */
#define GEM_DMACFG_RXBUF(n)    (((n) & 0xFFu) << GEM_DMACFG_RXBUF_SHIFT)
/** @} */

/**
 * @name    TX Status register bits (0x014, write-1-to-clear)
 * @{
 */
#define GEM_TXSTAT_USED_READ    (1u << 0)   /**< Used bit read (ring empty) */
#define GEM_TXSTAT_COLLISION    (1u << 1)   /**< Collision during TX */
#define GEM_TXSTAT_RETRY_LIMIT  (1u << 2)   /**< Retry limit exceeded */
#define GEM_TXSTAT_GO           (1u << 3)   /**< TX in progress (read-only) */
#define GEM_TXSTAT_CORRUPT      (1u << 4)   /**< TX frame corrupt (AHB error) */
#define GEM_TXSTAT_COMPLETE     (1u << 5)   /**< Frame transmitted */
#define GEM_TXSTAT_ERR_MASK     (GEM_TXSTAT_COLLISION | GEM_TXSTAT_RETRY_LIMIT | \
                                  GEM_TXSTAT_CORRUPT)
/** @} */

/**
 * @name    RX Status register bits (0x020, write-1-to-clear)
 * @{
 */
#define GEM_RXSTAT_NO_BUF       (1u << 0)   /**< No RX buffer available */
#define GEM_RXSTAT_FRAME_RECD   (1u << 1)   /**< Frame received */
#define GEM_RXSTAT_OVERRUN      (1u << 2)   /**< RX buffer overrun */
/** @} */

/**
 * @name    Interrupt Status/Enable/Disable bits (0x024/0x028/0x02C)
 * @{
 */
#define GEM_IRQ_MGMT_SENT       (1u << 0)   /**< Management frame sent */
#define GEM_IRQ_RX_COMPLETE     (1u << 1)   /**< Frame received */
#define GEM_IRQ_RX_USED_READ    (1u << 2)   /**< RX used bit read (no buffer) */
#define GEM_IRQ_TX_USED_READ    (1u << 3)   /**< TX used bit read (ring empty) */
#define GEM_IRQ_TX_RETRY_LIMIT  (1u << 4)   /**< TX retry limit exceeded */
#define GEM_IRQ_TX_CORRUPT      (1u << 5)   /**< TX frame corrupt */
#define GEM_IRQ_TX_COLLISION    (1u << 6)   /**< TX collision */
#define GEM_IRQ_TX_COMPLETE     (1u << 7)   /**< TX frame transmitted */
#define GEM_IRQ_RX_OVERRUN      (1u << 10)  /**< RX overrun */
#define GEM_IRQ_HRESP_ERR       (1u << 11)  /**< AHB bus error */
/** @} */

/**
 * @name    PHY Maintenance register (MDIO, 0x034)
 *
 * IEEE 802.3 Clause 22 frame format packed into one 32-bit register.
 * @{
 */
#define GEM_PHY_MAINT_MUST10    (1u << 30)              /**< Clause 22 SOF = 01 (upper bit) */
#define GEM_PHY_MAINT_SOF       (1u << 28)              /**< Clause 22 SOF lower bit */
#define GEM_PHY_MAINT_OP_READ   (2u << 28)              /**< Read operation code (10) */
#define GEM_PHY_MAINT_OP_WRITE  (1u << 28)              /**< Write operation code (01) */
#define GEM_PHY_MAINT_TA        (2u << 16)              /**< Turn-around bits "10" */
#define GEM_PHY_MAINT_PHYAD(a)  (((a) & 0x1Fu) << 23)  /**< PHY address field */
#define GEM_PHY_MAINT_REGAD(r)  (((r) & 0x1Fu) << 18)  /**< Register address field */
#define GEM_PHY_MAINT_DATA(d)   ((d) & 0xFFFFu)         /**< Data field */

/** Compose a read transaction */
#define GEM_MDIO_READ(phy, reg) \
    (GEM_PHY_MAINT_MUST10 | GEM_PHY_MAINT_OP_READ | GEM_PHY_MAINT_TA | \
     GEM_PHY_MAINT_PHYAD(phy) | GEM_PHY_MAINT_REGAD(reg))

/** Compose a write transaction */
#define GEM_MDIO_WRITE(phy, reg, data) \
    (GEM_PHY_MAINT_MUST10 | GEM_PHY_MAINT_OP_WRITE | GEM_PHY_MAINT_TA | \
     GEM_PHY_MAINT_PHYAD(phy) | GEM_PHY_MAINT_REGAD(reg) | GEM_PHY_MAINT_DATA(data))
/** @} */

/**
 * @name    RX buffer descriptor (8 bytes per entry)
 *
 * GEM fills these; CPU re-arms them after reading.
 * Bits[1:0] of addr are used as control flags (not part of address).
 * Buffer address must be 4-byte aligned.
 * @{
 */
typedef struct {
    volatile uint32_t addr;   /**< bits[31:2]=buf_phys>>2, bit[1]=wrap, bit[0]=used */
    volatile uint32_t status; /**< bits[12:0]=len, bit[14]=EOF, bit[15]=SOF */
} gem_rx_desc_t;

#define GEM_RX_USED             (1u << 0)   /**< CPU owns descriptor (1=done) */
#define GEM_RX_WRAP             (1u << 1)   /**< Last descriptor in ring */
#define GEM_RX_STATUS_LEN_MASK  (0x1FFFu)  /**< Frame length in status word */
#define GEM_RX_STATUS_EOF       (1u << 15)  /**< End of frame */
#define GEM_RX_STATUS_SOF       (1u << 14)  /**< Start of frame */
#define GEM_RX_STATUS_FCS_ERR   (1u << 25)  /**< FCS/CRC error */
/** @} */

/**
 * @name    TX buffer descriptor (8 bytes per entry)
 *
 * CPU fills these; GEM reads and transmits.
 * @{
 */
typedef struct {
    volatile uint32_t addr;   /**< Buffer address (PS DDR physical, full 32-bit) */
    volatile uint32_t ctrl;   /**< bits[13:0]=len, bit[15]=last_buf, bit[29]=wrap,
                                    bit[30]=used(1=CPU,0=GEM) */
} gem_tx_desc_t;

#define GEM_TX_USED             (1u << 31)  /**< CPU owns (GEM skips if set) */
#define GEM_TX_WRAP             (1u << 30)  /**< Last descriptor in ring */
#define GEM_TX_LAST_BUF         (1u << 15)  /**< Last buffer in frame */
#define GEM_TX_NO_CRC           (1u << 16)  /**< Do not append CRC */
#define GEM_TX_LEN_MASK         (0x3FFFu)   /**< Frame length bits[13:0] */
/** @} */

/**
 * @name    IEEE 802.3 MII register numbers
 * @{
 */
#define GEM_MII_CTRL            (0)     /**< Basic Control */
#define GEM_MII_STATUS          (1)     /**< Basic Status */
#define GEM_MII_PHYSID1         (2)     /**< PHY ID 1 */
#define GEM_MII_PHYSID2         (3)     /**< PHY ID 2 */
#define GEM_MII_ADV             (4)     /**< Auto-Negotiation Advertisement */
#define GEM_MII_LPA             (5)     /**< Link Partner Ability */

#define GEM_MII_CTRL_RST        (1u << 15) /**< Software reset */
#define GEM_MII_CTRL_ANEG_EN    (1u << 12) /**< Auto-negotiation enable */
#define GEM_MII_CTRL_RESTART_AN (1u << 9)  /**< Restart auto-negotiation (self-clearing) */
#define GEM_MII_CTRL_SPD100     (1u << 13) /**< 100 Mbps (forced mode) */
#define GEM_MII_CTRL_FD         (1u << 8)  /**< Full duplex (forced mode) */
#define GEM_MII_STAT_LINK       (1u << 2)  /**< Link status */
#define GEM_MII_STAT_ANEG_DONE  (1u << 5)  /**< Auto-negotiation complete */

#define GEM_MII_LPA_10HD        (1u << 5)  /**< 10BASE-T Half Duplex */
#define GEM_MII_LPA_10FD        (1u << 6)  /**< 10BASE-T Full Duplex */
#define GEM_MII_LPA_100HD       (1u << 7)  /**< 100BASE-TX Half Duplex */
#define GEM_MII_LPA_100FD       (1u << 8)  /**< 100BASE-TX Full Duplex */

#define GEM_MII_GBT_CTRL        (9)            /**< 1000BASE-T Control register */
#define GEM_MII_GBT_CTRL_1000HD (1u << 8)     /**< Advertise 1000BASE-T half duplex */
#define GEM_MII_GBT_CTRL_1000FD (1u << 9)     /**< Advertise 1000BASE-T full duplex */
#define GEM_MII_GBT_STATUS      (10)           /**< 1000BASE-T Status register */
#define GEM_MII_GBT_STAT_LP1000HD (1u << 10)  /**< Link partner: 1000BASE-T half duplex */
#define GEM_MII_GBT_STAT_LP1000FD (1u << 11)  /**< Link partner: 1000BASE-T full duplex */
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
