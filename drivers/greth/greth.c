/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     drivers_greth
 * @{
 *
 * @file
 * @brief       Gaisler GRETH Ethernet MAC netdev driver
 *
 * ## Driver architecture
 *
 * This is a legacy-mode netdev driver (confirm_send = NULL). The send()
 * function blocks until the DMA engine completes the transmission and returns
 * the number of bytes sent.
 *
 * RX is interrupt-driven: the PLIC calls _greth_isr() which signals
 * NETDEV_EVENT_RX_COMPLETE to the network stack. The stack then calls recv()
 * to copy the frame and re-arm the descriptor.
 *
 * ## Descriptor ring layout
 *
 * TX and RX descriptor tables plus their associated data buffers are declared
 * as file-scope static variables so they can carry their own alignment
 * attribute without propagating that alignment into greth_t (which would
 * trigger -Wcast-align when casting netdev_t* → greth_t*).
 *
 * @author      Matvii Ivashchenko
 * @}
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "net/netdev/eth.h"
#include "net/ethernet.h"
#include "iolist.h"

#include "plic.h"

#include "greth.h"
#include "greth_regs.h"

#define ENABLE_DEBUG    1
#include "debug.h"

/* -------------------------------------------------------------------------
 * Static descriptor tables and data buffers
 *
 * GRLIB requirement: the descriptor table base address must be aligned to
 * at least the table byte size. We use 1024 bytes, which covers up to
 * 128 descriptors × 8 bytes each.
 * ---------------------------------------------------------------------- */

static greth_desc_t _tx_desc[CONFIG_GRETH_TX_DESC_NUM]
    __attribute__((aligned(1024)));

static greth_desc_t _rx_desc[CONFIG_GRETH_RX_DESC_NUM]
    __attribute__((aligned(1024)));

static uint8_t _tx_buf[GRETH_BUF_SIZE];

static uint8_t _rx_buf[CONFIG_GRETH_RX_DESC_NUM][GRETH_BUF_SIZE];

/* -------------------------------------------------------------------------
 * D-cache coherency helpers (Zicbom extension)
 *
 * NOEL-V has a 4×4 kB, 32 B/line write-back data cache that is NOT coherent
 * with the GRETH DMA (AHB Master 1).  Without explicit cache management:
 *   - CPU writes to a descriptor/buffer stay in dirty cache lines; DMA reads
 *     physical DRAM and sees stale zeros → TX never starts.
 *   - DMA writes received data to DRAM; CPU reads from stale cache lines and
 *     sees old garbage → RX frame is corrupted.
 *
 * cbo.flush  — write dirty cache line to DRAM (keeps line in cache as clean)
 *              Call before DMA reads so DMA sees current CPU-written data.
 * cbo.inval  — discard the CPU's cached copy (line is gone from D-cache)
 *              Call before CPU reads so CPU fetches fresh DMA-written data.
 * ---------------------------------------------------------------------- */

#define GRETH_CACHE_LINE_SIZE   32u

static inline void _cbo_flush(void *addr)
{
    __asm__ volatile(
        ".option push\n\t"
        ".option arch, +zicbom\n\t"
        "cbo.flush 0(%0)\n\t"
        ".option pop"
        :: "r"(addr) : "memory");
}

static inline void _cbo_inval(void *addr)
{
    __asm__ volatile(
        ".option push\n\t"
        ".option arch, +zicbom\n\t"
        "cbo.inval 0(%0)\n\t"
        ".option pop"
        :: "r"(addr) : "memory");
}

static void _dcache_flush_range(const void *p, size_t len)
{
    uintptr_t addr = (uintptr_t)p & ~(uintptr_t)(GRETH_CACHE_LINE_SIZE - 1u);
    uintptr_t end  = (uintptr_t)p + len;
    while (addr < end) {
        _cbo_flush((void *)addr);
        addr += GRETH_CACHE_LINE_SIZE;
    }
}

static void _dcache_inval_range(const void *p, size_t len)
{
    uintptr_t addr = (uintptr_t)p & ~(uintptr_t)(GRETH_CACHE_LINE_SIZE - 1u);
    uintptr_t end  = (uintptr_t)p + len;
    while (addr < end) {
        _cbo_inval((void *)addr);
        addr += GRETH_CACHE_LINE_SIZE;
    }
}

/* -------------------------------------------------------------------------
 * Convenience macro
 * ---------------------------------------------------------------------- */

/**
 * @brief   Get the hardware register block pointer from a greth_t
 *
 * base_addr is uint32_t (APB address < 4 GB on GRLIB designs). We cast via
 * uintptr_t to silence the "int-to-pointer cast of different size" warning
 * that would occur on rv64 if we cast uint32_t directly to a pointer.
 */
#define GRETH_REGS(dev) \
    ((greth_regs_t *)(uintptr_t)((dev)->params->base_addr))

/* -------------------------------------------------------------------------
 * Low-level MMIO helpers
 * ---------------------------------------------------------------------- */

static inline void _mmio_write(volatile uint32_t *addr, uint32_t val)
{
    *addr = val;
}

static inline uint32_t _mmio_read(const volatile uint32_t *addr)
{
    return *addr;
}

/* -------------------------------------------------------------------------
 * MDIO helpers
 * ---------------------------------------------------------------------- */

/* Wait for any in-progress MDIO transaction to finish */
static void _mdio_wait(greth_regs_t *regs)
{
    for (unsigned i = 0; i < 200000u; i++) {
        if (!(_mmio_read(&regs->mdio) & GRETH_MDIO_BUSY)) {
            return;
        }
    }
}

/* Initiate an MDIO read and wait for completion using a two-phase poll.
 *
 * Phase 1: wait up to 10000 iterations for BUSY to go HIGH (transaction
 *          started by hardware).  If BUSY never rises, the MDIO bus is
 *          not accessible from this IP core — return -2.
 * Phase 2: wait up to 200000 iterations for BUSY to go LOW (done).
 *
 * Returns the 16-bit register value, or < 0 on error:
 *   -1  NVALID set (no PHY at this address)
 *   -2  MDIO not accessible (BUSY never asserted)
 *   -3  MDIO stuck busy (transaction never completed) */
static int _mdio_read(greth_regs_t *regs, unsigned phy_addr, unsigned reg)
{
    _mdio_wait(regs);

    uint32_t cmd = (phy_addr << GRETH_MDIO_PHYSHIFT) |
                   (reg      << GRETH_MDIO_REGSHIFT)  |
                   GRETH_MDIO_OP_RD;
    _mmio_write(&regs->mdio, cmd);

    /* Phase 1: confirm the transaction started (BUSY goes high) */
    bool busy_seen = false;
    for (unsigned i = 0; i < 10000u; i++) {
        if (_mmio_read(&regs->mdio) & GRETH_MDIO_BUSY) {
            busy_seen = true;
            break;
        }
    }
    if (!busy_seen) {
        return -2;  /* MDIO not routed / not accessible */
    }

    /* Phase 2: wait for transaction to complete (BUSY goes low) */
    for (unsigned i = 0; i < 200000u; i++) {
        uint32_t val = _mmio_read(&regs->mdio);
        if (!(val & GRETH_MDIO_BUSY)) {
            if (val & GRETH_MDIO_NVALID) {
                return -1;
            }
            return (int)((val >> GRETH_MDIO_DATASHIFT) & 0xFFFF);
        }
    }
    return -3;  /* MDIO transaction never completed */
}

static void _mdio_write_reg(greth_regs_t *regs, unsigned phy_addr,
                            unsigned reg, uint16_t data)
{
    _mdio_wait(regs);

    uint32_t cmd = ((uint32_t)data  << GRETH_MDIO_DATASHIFT) |
                   (phy_addr        << GRETH_MDIO_PHYSHIFT)   |
                   (reg             << GRETH_MDIO_REGSHIFT)   |
                   GRETH_MDIO_OP_WR;
    _mmio_write(&regs->mdio, cmd);
    _mdio_wait(regs);
}

/* -------------------------------------------------------------------------
 * PHY management
 * ---------------------------------------------------------------------- */

static unsigned _phy_detect(greth_regs_t *regs)
{
    /* The MDIO register bits[15:11] hold the address from the last MDIO
     * operation — when GRMON/EDCL left the PHY in a known state, this gives
     * the correct address without needing a scan. */
    uint32_t mdio = _mmio_read(&regs->mdio);
    unsigned phy_addr = (mdio >> GRETH_MDIO_PHYSHIFT) & 0x1F;

    int status = _mdio_read(regs, phy_addr, GRETH_MII_STATUS);
    /* status == 0: MDIO returned all-zeros — no real IEEE 802.3 PHY (bit 0 =
     * extended-capable is always 1).  Fall through to the full scan. */
    if (status > 0) {
        printf("[greth] PHY found at MDIO addr %u (from register), status=0x%04x\n",
               phy_addr, status);
        return phy_addr;
    }

    /* Full scan: try all 32 addresses.  Stop early if MDIO is inaccessible
     * (-2) — no point scanning all 32 if BUSY never asserts. */
    for (unsigned i = 0; i < 32; i++) {
        status = _mdio_read(regs, i, GRETH_MII_STATUS);
        if (status == -2) {
            printf("[greth] MDIO bus not accessible (BUSY never asserted)\n");
            break;
        }
        if (status > 0 && status != 0xFFFF) {
            printf("[greth] PHY found at MDIO addr %u (scan), status=0x%04x\n",
                   i, status);
            return i;
        }
    }
    printf("[greth] WARNING: no PHY via MDIO; mdio_reg=0x%08" PRIx32 "\n", mdio);
    return 0;
}

static void _phy_reset_and_aneg(greth_t *dev)
{
    greth_regs_t *regs = GRETH_REGS(dev);

    /* Software reset the PHY (bit 15 of MII Control register) */
    _mdio_write_reg(regs, dev->phy_addr, GRETH_MII_CTRL, GRETH_MII_CTRL_RST);

    /* Wait for reset to self-clear */
    for (unsigned i = 0; i < GRETH_ANEG_TIMEOUT; i++) {
        int ctrl = _mdio_read(regs, dev->phy_addr, GRETH_MII_CTRL);
        if (ctrl >= 0 && !(ctrl & GRETH_MII_CTRL_RST)) {
            break;
        }
    }

    /* Check if auto-negotiation is enabled in PHY (bit 12) */
    int ctrl = _mdio_read(regs, dev->phy_addr, GRETH_MII_CTRL);
    if (ctrl < 0 || !(ctrl & GRETH_MII_CTRL_ANEG)) {
        return;
    }

    /* Wait for auto-negotiation complete (MII Status reg bit 5) */
    for (unsigned i = 0; i < GRETH_ANEG_TIMEOUT; i++) {
        int status = _mdio_read(regs, dev->phy_addr, GRETH_MII_STATUS);
        if (status >= 0 && (status & GRETH_MII_STATUS_ANEG_DONE)) {
            return;
        }
    }
    DEBUG("[greth] auto-negotiation timeout — continuing anyway\n");
}

static void _phy_configure_mac(greth_t *dev)
{
    greth_regs_t *regs = GRETH_REGS(dev);

    int phy_ctrl = _mdio_read(regs, dev->phy_addr, GRETH_MII_CTRL);
    int phy_stat = _mdio_read(regs, dev->phy_addr, GRETH_MII_STATUS);

    bool full_duplex;
    bool speed_100;

    if (phy_ctrl == -2 || phy_stat == -2) {
        /* MDIO bus is not accessible from this IP core (BUSY never asserted).
         * This happens when the MDIO pins are not routed to the PL on the
         * ZedBoard NOEL-V design and GRMON configured the PHY via the PS.
         * Fall back to the speed that GRMON would have negotiated: 100 Mbps
         * full-duplex is the standard auto-neg result for this PHY/switch. */
        printf("[greth] MDIO not accessible — defaulting to 100 Mbps full-duplex\n");
        full_duplex = true;
        speed_100   = true;
    }
    else if (phy_ctrl > 0 && (phy_ctrl & GRETH_MII_CTRL_ANEG)) {
        /* Auto-negotiation was used.  MII_CTRL bits 13/8 are the *forced-mode*
         * configuration — they do NOT update to reflect the negotiated result.
         * Read LPA (reg 5) and ADV (reg 4), AND them together to find the
         * highest common capability. */
        int adv = _mdio_read(regs, dev->phy_addr, GRETH_MII_ADV);
        int lpa = _mdio_read(regs, dev->phy_addr, GRETH_MII_LPA);
        if (adv < 0) adv = 0;
        if (lpa < 0) lpa = 0;
        int common = adv & lpa;

        printf("[greth] PHY addr=%u ctrl=0x%04x stat=0x%04x adv=0x%04x lpa=0x%04x\n",
               dev->phy_addr, phy_ctrl, phy_stat, adv, lpa);

        if (common & GRETH_MII_LPA_100_FD) {
            speed_100 = true; full_duplex = true;
        }
        else if (common & GRETH_MII_LPA_100_HD) {
            speed_100 = true; full_duplex = false;
        }
        else if (common & GRETH_MII_LPA_10_FD) {
            speed_100 = false; full_duplex = true;
        }
        else {
            speed_100 = false; full_duplex = false;
        }
    }
    else if (phy_ctrl > 0) {
        /* Forced mode: bits 13 and 8 of MII_CTRL are valid */
        full_duplex = (phy_ctrl & GRETH_MII_CTRL_FD)     != 0;
        speed_100   = (phy_ctrl & GRETH_MII_CTRL_SPD100) != 0;
        printf("[greth] PHY addr=%u ctrl=0x%04x stat=0x%04x (forced mode)\n",
               dev->phy_addr, phy_ctrl, phy_stat);
    }
    else {
        /* MDIO returned unexpected value (0 or error other than -2).
         * Fall back to safe default. */
        printf("[greth] PHY MDIO read unexpected (ctrl=%d stat=%d), defaulting 100FD\n",
               phy_ctrl, phy_stat);
        full_duplex = true;
        speed_100   = true;
    }

    uint32_t mac_ctrl = GRETH_CTRL_EDCLDIS;
    if (full_duplex) mac_ctrl |= GRETH_CTRL_FD;
    if (speed_100)   mac_ctrl |= GRETH_CTRL_SPD;
    _mmio_write(&regs->ctrl, mac_ctrl);

    printf("[greth] MAC ctrl=0x%08" PRIx32 " => %s duplex, %s Mbps\n",
           mac_ctrl,
           full_duplex ? "full" : "half",
           speed_100   ? "100" : "10");
}

/* -------------------------------------------------------------------------
 * MAC address
 * ---------------------------------------------------------------------- */

static void _set_mac_address(greth_t *dev)
{
    greth_regs_t *regs = GRETH_REGS(dev);
    const uint8_t *mac = dev->params->mac;

    _mmio_write(&regs->mac_msb,
                ((uint32_t)mac[0] << 8) | mac[1]);
    _mmio_write(&regs->mac_lsb,
                ((uint32_t)mac[2] << 24) |
                ((uint32_t)mac[3] << 16) |
                ((uint32_t)mac[4] <<  8) |
                 (uint32_t)mac[5]);
}

/* -------------------------------------------------------------------------
 * Descriptor ring initialisation
 * ---------------------------------------------------------------------- */

static void _tx_desc_init(greth_t *dev)
{
    for (unsigned i = 0; i < CONFIG_GRETH_TX_DESC_NUM; i++) {
        uint32_t ctrl = 0;  /* EN=0: free, DMA will not touch this */
        if (i == CONFIG_GRETH_TX_DESC_NUM - 1) {
            ctrl |= GRETH_BD_WR;
        }
        dev->tx_desc[i].ctrl = ctrl;
        dev->tx_desc[i].addr = 0;
    }
    dev->tx_idx = 0;
    _dcache_flush_range(dev->tx_desc,
                        CONFIG_GRETH_TX_DESC_NUM * sizeof(greth_desc_t));
}

static void _rx_desc_init(greth_t *dev)
{
    for (unsigned i = 0; i < CONFIG_GRETH_RX_DESC_NUM; i++) {
        uint32_t ctrl = GRETH_BD_EN | GRETH_BD_IE;
        if (i == CONFIG_GRETH_RX_DESC_NUM - 1) {
            ctrl |= GRETH_BD_WR;
        }
        dev->rx_desc[i].ctrl = ctrl;
        dev->rx_desc[i].addr = (uint32_t)(uintptr_t)_rx_buf[i];
    }
    dev->rx_idx = 0;
    _dcache_flush_range(dev->rx_desc,
                        CONFIG_GRETH_RX_DESC_NUM * sizeof(greth_desc_t));
}

/* -------------------------------------------------------------------------
 * Interrupt handler
 * ---------------------------------------------------------------------- */

/* Single-instance global — allows the PLIC callback to reach our device */
static greth_t *_greth_dev_ptr;

/* Pending status bits accumulated in PLIC ISR context, consumed in _isr().
 * Volatile because it is written from ISR and read from the netif thread. */
static volatile uint32_t _greth_pending_status;

static void _greth_isr(int irq)
{
    (void)irq;

    greth_t *dev = _greth_dev_ptr;
    if (!dev || !dev->netdev.event_callback) {
        return;
    }

    greth_regs_t *regs = GRETH_REGS(dev);
    uint32_t status = _mmio_read(&regs->status);

    /* Write-1-to-clear: must happen in ISR before plic_complete_interrupt().
     * GRETH is level-triggered at the PLIC; leaving status bits set would
     * cause the interrupt to re-fire immediately after claim/complete. */
    _mmio_write(&regs->status, status);

    /* Accumulate for _isr() which runs in the netif thread */
    _greth_pending_status |= status;

    /* Signal the netif thread — event_post() is ISR-safe */
    dev->netdev.event_callback(&dev->netdev, NETDEV_EVENT_ISR);
}

/* -------------------------------------------------------------------------
 * netdev operations
 * ---------------------------------------------------------------------- */

static int _init(netdev_t *netdev)
{
    /* Cast through void* to suppress -Wcast-align: greth_t is always the
     * containing object; netdev is its first member, so addresses match. */
    greth_t *dev = (greth_t *)(void *)netdev;
    greth_regs_t *regs = GRETH_REGS(dev);

    /* Wire static arrays into the device descriptor */
    dev->tx_desc = _tx_desc;
    dev->rx_desc = _rx_desc;
    dev->tx_buf  = _tx_buf;

    /* Read hardware capabilities */
    uint32_t cap = _mmio_read(&regs->ctrl);
    dev->gbit    = (cap & GRETH_CTRL_GBIT_CAP) != 0;
    bool has_edcl = (cap & GRETH_CTRL_EDCL_CAP) != 0;
    printf("[greth] cap=0x%08" PRIx32 " gbit=%d has_edcl=%d\n",
           cap, dev->gbit, (int)has_edcl);

    /* Software reset: clears all GRETH registers and the TX/RX DMA state
     * machines (including the DMA's internal current-descriptor pointer).
     * This is required on every boot because grmon 'run' does NOT reset
     * peripherals, so the DMA pointer from a previous run is stale.
     * Per GRETH spec, the RST bit does NOT affect the EDCL state machine,
     * so GRMON's debug link remains active through this reset. */
    _mmio_write(&regs->ctrl, GRETH_CTRL_RST);
    for (unsigned i = 0; i < 100000u; i++) {
        if (!(_mmio_read(&regs->ctrl) & GRETH_CTRL_RST)) {
            break;
        }
    }
    printf("[greth] SW reset done: ctrl=0x%08" PRIx32 "\n",
           _mmio_read(&regs->ctrl));

    /* Detect PHY MDIO address */
    dev->phy_addr = _phy_detect(regs);
    DEBUG("[greth] PHY at MDIO address %u\n", dev->phy_addr);

    /* Reset PHY and negotiate link speed/duplex */
    _phy_reset_and_aneg(dev);
    _phy_configure_mac(dev);

    /* Set MAC address */
    _set_mac_address(dev);

    /* Initialise TX and RX descriptor rings */
    _tx_desc_init(dev);
    _rx_desc_init(dev);

    /* Tell DMA engine where the descriptor tables are */
    _mmio_write(&regs->tx_desc, (uint32_t)(uintptr_t)dev->tx_desc);
    _mmio_write(&regs->rx_desc, (uint32_t)(uintptr_t)dev->rx_desc);

    /* Register PLIC interrupt */
    _greth_dev_ptr = dev;
    plic_set_priority(dev->params->irq, 1);
    plic_set_isr_cb(dev->params->irq, _greth_isr);
    plic_enable_interrupt(dev->params->irq);

    /* Enable RX DMA and RX interrupt */
    uint32_t ctrl = _mmio_read(&regs->ctrl);
    _mmio_write(&regs->ctrl, ctrl | GRETH_CTRL_RXEN | GRETH_CTRL_RXIRQEN);

    printf("[greth] init done: ctrl=0x%08" PRIx32
           " tx_desc=0x%08" PRIx32 "(hw=0x%08" PRIx32 ")"
           " rx_desc=0x%08" PRIx32 "(hw=0x%08" PRIx32 ")"
           " gbit=%d\n",
           _mmio_read(&regs->ctrl),
           (uint32_t)(uintptr_t)dev->tx_desc, _mmio_read(&regs->tx_desc),
           (uint32_t)(uintptr_t)dev->rx_desc, _mmio_read(&regs->rx_desc),
           dev->gbit);

    /* Trigger a synthetic ISR event so the netif thread (after it releases
     * its init mutex and enters its event loop) will call _isr(), which in
     * turn emits NETDEV_EVENT_LINK_UP.  We cannot emit LINK_UP directly here
     * because gnrc_netif holds gnrc_netif_acquire() during init(), and
     * gnrc_ipv6_nib_iface_up() (triggered by LINK_UP) also calls
     * gnrc_netif_acquire() — causing a deadlock.  Posting NETDEV_EVENT_ISR
     * via event_post() is non-blocking and safe from this context. */
    if (dev->netdev.event_callback) {
        dev->netdev.event_callback(&dev->netdev, NETDEV_EVENT_ISR);
    }

    return 0;
}

static int _send(netdev_t *netdev, const iolist_t *iolist)
{
    greth_t *dev = (greth_t *)(void *)netdev;
    greth_regs_t *regs = GRETH_REGS(dev);

    /* Flatten scatter-gather list into a single flat TX buffer */
    ssize_t total = iolist_to_buffer(iolist, dev->tx_buf, GRETH_BUF_SIZE);
    if (total < 0) {
        DEBUG("[greth] send: frame too large\n");
        return -ENOBUFS;
    }

    /* Pad to minimum Ethernet frame size */
    if ((size_t)total < ETHERNET_MIN_LEN) {
        memset(dev->tx_buf + total, 0, ETHERNET_MIN_LEN - (size_t)total);
        total = ETHERNET_MIN_LEN;
    }

    unsigned idx = dev->tx_idx;
    greth_desc_t *desc = &dev->tx_desc[idx];

    /* Wait until descriptor is free (DMA clears EN when done) */
    for (unsigned i = 0; i < 1000000u; i++) {
        if (!(_mmio_read(&desc->ctrl) & GRETH_BD_EN)) {
            break;
        }
    }

    /* Arm descriptor: set buffer address and length, enable for DMA.
     * IE=1: ask DMA to set GRETH_STATUS_TXIRQ in the status register on
     * completion.  This lets us detect TX done via the APB status register
     * (non-cached MMIO) independently of cbo_inval correctness. */
    desc->addr = (uint32_t)(uintptr_t)dev->tx_buf;
    uint32_t ctrl = (uint32_t)total | GRETH_BD_EN | GRETH_BD_IE;
    if (idx == CONFIG_GRETH_TX_DESC_NUM - 1) {
        ctrl |= GRETH_BD_WR;
    }
    desc->ctrl = ctrl;

    dev->tx_idx = (idx + 1) % CONFIG_GRETH_TX_DESC_NUM;

    /* Write TX buffer and descriptor from D-cache to physical DRAM so that
     * the GRETH DMA (a separate AHB master) sees current data when it reads
     * the descriptor table and fetches the frame payload. */
    _dcache_flush_range(dev->tx_buf, (size_t)total);
    _dcache_flush_range(desc, sizeof(*desc));

    /* Fence: ensure all cache-flush AHB writes reach SDRAM before the
     * subsequent APB write that kicks the DMA.  Without this, the processor's
     * store buffer may reorder the TXEN write ahead of the flush. */
    __asm__ volatile("fence ow, ow" ::: "memory");

    /* STEP 1: verify flush reached DRAM */
    _cbo_inval((void *)desc);
    uint32_t verify_ctrl = _mmio_read(&desc->ctrl);
    printf("[greth] TX[%u] S1-FLUSH: armed=0x%08" PRIx32 " dram=0x%08" PRIx32
           " buf=0x%08" PRIx32 " hw_tdesc=0x%08" PRIx32 "\n",
           idx, ctrl, verify_ctrl,
           (uint32_t)(uintptr_t)dev->tx_buf, _mmio_read(&regs->tx_desc));

    /* STEP 2: clear TXEN — write only the bits we actually set (no cap bits from readback) */
    uint32_t mac_ctrl = _mmio_read(&regs->ctrl);
    uint32_t clean_ctrl = mac_ctrl & (GRETH_CTRL_FD | GRETH_CTRL_PRO |
                                      GRETH_CTRL_SPD | GRETH_CTRL_GB |
                                      GRETH_CTRL_RXEN | GRETH_CTRL_RXIRQEN |
                                      GRETH_CTRL_TXIRQEN | GRETH_CTRL_TXEN);
    _mmio_write(&regs->ctrl, clean_ctrl & ~GRETH_CTRL_TXEN);
    printf("[greth] TX[%u] S2-TXEN0: ctrl=0x%08" PRIx32 " status=0x%08" PRIx32
           " (clean=0x%08" PRIx32 ")\n",
           idx, _mmio_read(&regs->ctrl), _mmio_read(&regs->status), clean_ctrl);

    /* STEP 3: set TXEN + TXIRQEN — no cap bits from readback */
    _mmio_write(&regs->ctrl, clean_ctrl | GRETH_CTRL_TXEN | GRETH_CTRL_TXIRQEN);
    {
        volatile uint32_t *ahbstat  = (volatile uint32_t *)0xff982000u;
        volatile uint32_t *ahbaddr  = (volatile uint32_t *)0xff982004u;
        printf("[greth] TX[%u] S3-TXEN1: ctrl=0x%08" PRIx32 " status=0x%08" PRIx32
               " ahbstat=0x%08" PRIx32 " ahbaddr=0x%08" PRIx32 "\n",
               idx, _mmio_read(&regs->ctrl), _mmio_read(&regs->status),
               *ahbstat, *ahbaddr);
    }

    /* STEP 4: immediately read descriptor — DMA may process it within a few cycles */
    _cbo_inval((void *)desc);
    __asm__ volatile("fence ir, ir" ::: "memory");
    printf("[greth] TX[%u] S4-IMM:   desc=0x%08" PRIx32 "\n",
           idx, _mmio_read(&desc->ctrl));

    /* STEP 5: wait loop.
     * Two independent exit conditions:
     *   A) TXIRQ or TXERR in the APB status register (non-cached MMIO).
     *      With IE=1 in descriptor, DMA sets TXIRQ on success.
     *      TXERR is set on DMA error regardless of IE.
     *      If A fires but desc EN is still 1, cbo_inval is broken and TX
     *      actually completed — caches are the only remaining problem.
     *   B) Descriptor EN=0 via cbo_inval + fresh DRAM read. */
    unsigned tx_wait;
    bool status_exit = false;
    bool txen_cleared = false;
    static const unsigned _samples[] = { 100, 1000, 10000, 100000, 500000, 999999 };
    unsigned _si = 0;
    for (tx_wait = 0; tx_wait < 1000000u; tx_wait++) {
        /* A: status register (APB MMIO — always reads from hardware) */
        uint32_t s = _mmio_read(&regs->status);
        if (s & (GRETH_STATUS_TXIRQ | GRETH_STATUS_TXERR)) {
            _mmio_write(&regs->status, s);   /* W1C — clear the bits */
            printf("[greth] TX[%u] STATUS-EXIT[%u]: status=0x%08" PRIx32 "\n",
                   idx, tx_wait, s);
            status_exit = true;
            break;
        }

        /* A2: TXEN auto-clear — DMA sets TXEN=0 when it runs out of EN=1 descriptors.
         * If TXEN clears, DMA IS running but processed a descriptor other than ours
         * (wrong tx_desc pointer — possibly reading from ROM at 0x00000000 after reset). */
        uint32_t c = _mmio_read(&regs->ctrl);
        if (!(c & GRETH_CTRL_TXEN)) {
            printf("[greth] TX[%u] TXEN-CLEARED[%u]: ctrl=0x%08" PRIx32
                   " status=0x%08" PRIx32 " hw_tdesc=0x%08" PRIx32 "\n",
                   idx, tx_wait, c, _mmio_read(&regs->status),
                   _mmio_read(&regs->tx_desc));
            txen_cleared = true;
            break;
        }

        /* B: descriptor (DRAM — must invalidate cache first) */
        _cbo_inval((void *)desc);
        __asm__ volatile("fence ir, ir" ::: "memory");
        uint32_t d = _mmio_read(&desc->ctrl);
        if (!(d & GRETH_BD_EN)) {
            break;
        }

        if (_si < 6 && tx_wait == _samples[_si]) {
            printf("[greth] TX[%u] S5-POLL[%u]: desc=0x%08" PRIx32
                   " ctrl=0x%08" PRIx32 " status=0x%08" PRIx32 "\n",
                   idx, tx_wait, d,
                   _mmio_read(&regs->ctrl), _mmio_read(&regs->status));
            _si++;
        }
    }

    _cbo_inval((void *)desc);
    uint32_t post_ctrl = _mmio_read(&desc->ctrl);
    {
        volatile uint32_t *ahbstat  = (volatile uint32_t *)0xff982000u;
        volatile uint32_t *ahbaddr  = (volatile uint32_t *)0xff982004u;
        uint32_t ahbs = *ahbstat;
        uint32_t ahba = *ahbaddr;
        printf("[greth] TX[%u] DONE: wait=%u via=%s desc=0x%08" PRIx32
               " ctrl=0x%08" PRIx32 " status=0x%08" PRIx32
               " ahbstat=0x%08" PRIx32 " ahbaddr=0x%08" PRIx32 "\n",
               idx, tx_wait, status_exit ? "STATUS" : "DESC",
               post_ctrl, _mmio_read(&regs->ctrl), _mmio_read(&regs->status),
               ahbs, ahba);
        /* AHBSTAT[0]=NE (new error), [2:1]=CE/HWRITE, [6:3]=HMASTER */
        if (ahbs & 1u) {
            printf("[greth] AHBSTAT: AHB error! master=%u write=%u addr=0x%08" PRIx32 "\n",
                   (unsigned)((ahbs >> 3) & 0xFu), (unsigned)((ahbs >> 2) & 1u), ahba);
        }
    }

    if (!status_exit && !txen_cleared && (post_ctrl & GRETH_BD_EN)) {
        printf("[greth] TX[%u] TIMEOUT (DMA never ran)\n", idx);
        return -ETIMEDOUT;
    }
    if (txen_cleared) {
        /* DMA ran but processed a descriptor at wrong address (tx_desc pointer bug).
         * Our descriptor at 0x%08x was not touched (EN still 1). */
        printf("[greth] TX[%u] DMA-WRONG-ADDR: TXEN cleared but our desc EN=1\n", idx);
        return -EIO;
    }
    if (status_exit && (post_ctrl & GRETH_BD_EN)) {
        printf("[greth] TX[%u] STATUS-OK but desc EN=1 (cbo_inval broken?)\n", idx);
        /* TX actually worked — fall through to success path */
    }

    if (post_ctrl & GRETH_TXBD_ERR_MASK) {
        printf("[greth] TX error bits: 0x%08" PRIx32 "\n", post_ctrl & GRETH_TXBD_ERR_MASK);
        return -EIO;
    }

    return (int)total;
}

static int _recv(netdev_t *netdev, void *buf, size_t len, void *info)
{
    (void)info;
    greth_t *dev = (greth_t *)(void *)netdev;

    unsigned idx = dev->rx_idx;
    greth_desc_t *desc = &dev->rx_desc[idx];

    /* Invalidate CPU's cached copy of the descriptor so we see the DMA's
     * write (EN=0 + frame length) rather than our stale EN=1 from init. */
    _cbo_inval((void *)desc);
    uint32_t ctrl = _mmio_read(&desc->ctrl);

    /* EN=1 means DMA still owns this descriptor — nothing received yet */
    if (ctrl & GRETH_BD_EN) {
        return 0;
    }

    int frame_len = (int)(ctrl & GRETH_BD_LEN_MASK);

    /* Query mode: just return the frame size without consuming */
    if (buf == NULL && len == 0) {
        return frame_len;
    }

    /* If errors, discard and rearm */
    if (ctrl & GRETH_RXBD_ERR_MASK) {
        DEBUG("[greth] RX error ctrl=0x%08" PRIx32 "\n", ctrl);
        frame_len = -EIO;
        goto rearm;
    }

    /* Drop mode (buf=NULL, len>0) or buffer too small */
    if (buf == NULL || (size_t)frame_len > len) {
        if (buf != NULL) {
            frame_len = -ENOBUFS;
        }
        goto rearm;
    }

    /* Invalidate CPU's cached view of the RX buffer so memcpy reads the
     * frame bytes written to physical DRAM by the DMA, not stale cache. */
    _dcache_inval_range(_rx_buf[idx], (size_t)frame_len);

    /* Copy received frame to caller's buffer */
    memcpy(buf, _rx_buf[idx], (size_t)frame_len);

rearm:
    /* Re-arm descriptor: point back to the same static buffer, set EN=1 */
    {
        uint32_t new_ctrl = GRETH_BD_EN | GRETH_BD_IE;
        if (idx == CONFIG_GRETH_RX_DESC_NUM - 1) {
            new_ctrl |= GRETH_BD_WR;
        }
        desc->addr = (uint32_t)(uintptr_t)_rx_buf[idx];
        desc->ctrl = new_ctrl;
        /* Flush to DRAM so DMA sees EN=1 and uses the rearmed descriptor */
        _dcache_flush_range(desc, sizeof(*desc));
    }

    dev->rx_idx = (idx + 1) % CONFIG_GRETH_RX_DESC_NUM;

    /* Restart RX DMA in case it stalled (all descriptors were EN=0) */
    greth_regs_t *regs = GRETH_REGS(dev);
    uint32_t mac_ctrl = _mmio_read(&regs->ctrl);
    _mmio_write(&regs->ctrl, mac_ctrl | GRETH_CTRL_RXEN);

    return frame_len;
}

static void _isr(netdev_t *netdev)
{
    /* First synthetic call (posted from _init()): emit LINK_UP.
     * At this point gnrc_netif has already released its mutex, so
     * gnrc_ipv6_nib_iface_up() can safely acquire it — no deadlock. */
    static bool _link_up_sent = false;

    if (!_link_up_sent) {
        _link_up_sent = true;
        netdev->event_callback(netdev, NETDEV_EVENT_LINK_UP);
        return;
    }

    /* Subsequent calls: drain status bits accumulated by _greth_isr() */
    uint32_t status = _greth_pending_status;
    _greth_pending_status = 0;

    if (status & GRETH_STATUS_RXIRQ) {
        netdev->event_callback(netdev, NETDEV_EVENT_RX_COMPLETE);
    }
    if (status & (GRETH_STATUS_RXERR | GRETH_STATUS_TXERR)) {
        DEBUG("[greth] error status: 0x%08" PRIx32 "\n", status);
    }
}

static int _get(netdev_t *netdev, netopt_t opt, void *val, size_t max_len)
{
    greth_t *dev = (greth_t *)(void *)netdev;

    if (opt == NETOPT_ADDRESS) {
        if (max_len < ETHERNET_ADDR_LEN) {
            return -ENOBUFS;
        }
        memcpy(val, dev->params->mac, ETHERNET_ADDR_LEN);
        return ETHERNET_ADDR_LEN;
    }

    return netdev_eth_get(netdev, opt, val, max_len);
}

static int _set(netdev_t *netdev, netopt_t opt, const void *val, size_t val_len)
{
    greth_t *dev = (greth_t *)(void *)netdev;

    if (opt == NETOPT_PROMISCUOUSMODE) {
        greth_regs_t *regs = GRETH_REGS(dev);
        uint32_t ctrl = _mmio_read(&regs->ctrl);
        if (*(const netopt_enable_t *)val == NETOPT_ENABLE) {
            ctrl |= GRETH_CTRL_PRO;
        }
        else {
            ctrl &= ~GRETH_CTRL_PRO;
        }
        _mmio_write(&regs->ctrl, ctrl);
        return sizeof(netopt_enable_t);
    }

    return netdev_eth_set(netdev, opt, val, val_len);
}

/* -------------------------------------------------------------------------
 * Driver vtable and public setup function
 * ---------------------------------------------------------------------- */

static const netdev_driver_t _greth_driver = {
    .init         = _init,
    .send         = _send,
    .recv         = _recv,
    .isr          = _isr,
    .get          = _get,
    .set          = _set,
    .confirm_send = NULL,   /* legacy mode: send() returns byte count directly */
};

void greth_setup(greth_t *dev, const greth_params_t *params, uint8_t index)
{
    assert(dev);
    assert(params);

    dev->params        = params;
    dev->netdev.driver = &_greth_driver;
    dev->tx_desc       = NULL;  /* wired to _tx_desc in _init() */
    dev->rx_desc       = NULL;  /* wired to _rx_desc in _init() */
    dev->tx_buf        = NULL;  /* wired to _tx_buf  in _init() */
    dev->rx_idx        = 0;
    dev->tx_idx        = 0;
    dev->phy_addr      = 0;
    dev->gbit          = false;

    netdev_register(&dev->netdev, NETDEV_ANY, index);
}
