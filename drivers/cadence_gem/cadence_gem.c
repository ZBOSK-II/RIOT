/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     drivers_cadence_gem
 * @{
 *
 * @file
 * @brief       Cadence GEM Ethernet MAC netdev driver for NOEL-V on ZedBoard
 *
 * ## Architecture
 *
 * Legacy-mode netdev driver (confirm_send = NULL). send() blocks until the
 * GEM DMA completes transmission.
 *
 * RX uses a polling thread (Phase 1): a dedicated thread checks the RX
 * descriptor ring every 1 ms and posts NETDEV_EVENT_ISR to the netif thread
 * when a frame is available.
 *
 * ## DMA address space
 *
 * GEM DMA descriptors and data buffers live in NOEL-V physical address space
 * (RAM starts at 0x00800000). GEM DMA needs PS DDR addresses. The hardware
 * mapping is: PS_ADDR = 0x10000000 | (NOELV_ADDR & 0x0FFFFFFF).
 * Use GEM_TO_PS_PHYS() when writing addresses into descriptors.
 *
 * ## Cache coherency
 *
 * NOEL-V has a non-coherent write-back D-cache (32 B cache lines, Zicbom).
 * Use cbo.flush before DMA reads (TX), cbo.inval before CPU reads DMA output (RX).
 *
 * @author      Matvii Ivashchenko
 * @}
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "net/netdev/eth.h"
#include "net/ethernet.h"
#include "iolist.h"
#include "thread.h"
#include "ztimer.h"

#include "cadence_gem.h"
#include "cadence_gem_regs.h"

#define ENABLE_DEBUG    1
#include "debug.h"

/* -------------------------------------------------------------------------
 * Static descriptor rings and data buffers
 *
 * Cadence GEM requires the descriptor ring base address to be 4-byte aligned.
 * We use 1024-byte alignment to be safe and consistent with the GRETH driver.
 * ------------------------------------------------------------------------- */

static gem_rx_desc_t _rx_desc[CONFIG_GEM_RX_DESC_NUM]
    __attribute__((aligned(1024)));

static gem_tx_desc_t _tx_desc[CONFIG_GEM_TX_DESC_NUM]
    __attribute__((aligned(1024)));

static uint8_t _tx_buf[GEM_BUF_SIZE];

static uint8_t _rx_buf[CONFIG_GEM_RX_DESC_NUM][GEM_BUF_SIZE];

/* -------------------------------------------------------------------------
 * RX polling thread
 * ------------------------------------------------------------------------- */

#define _RX_POLL_STACK_SIZE     (THREAD_STACKSIZE_DEFAULT)
static char _rx_poll_stack[_RX_POLL_STACK_SIZE];
static gem_t *_gem_dev_ptr;

/* -------------------------------------------------------------------------
 * D-cache coherency helpers (Zicbom extension)
 *
 * NOEL-V D-cache is non-coherent with GEM DMA. Must flush before DMA reads
 * (TX path) and invalidate before CPU reads DMA-written data (RX path).
 * ------------------------------------------------------------------------- */

#define GEM_CACHE_LINE_SIZE     32u

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
    uintptr_t addr = (uintptr_t)p & ~(uintptr_t)(GEM_CACHE_LINE_SIZE - 1u);
    uintptr_t end  = (uintptr_t)p + len;
    while (addr < end) {
        _cbo_flush((void *)addr);
        addr += GEM_CACHE_LINE_SIZE;
    }
}

static void _dcache_inval_range(const void *p, size_t len)
{
    uintptr_t addr = (uintptr_t)p & ~(uintptr_t)(GEM_CACHE_LINE_SIZE - 1u);
    uintptr_t end  = (uintptr_t)p + len;
    while (addr < end) {
        _cbo_inval((void *)addr);
        addr += GEM_CACHE_LINE_SIZE;
    }
}

/* -------------------------------------------------------------------------
 * Convenience macros
 * ------------------------------------------------------------------------- */

#define GEM_REGS(dev) \
    ((gem_regs_t *)(uintptr_t)((dev)->params->base_addr))

static inline void _mmio_write(volatile uint32_t *addr, uint32_t val)
{
    *addr = val;
    /* cbo.flush writes the dirty cache line back to the device and clears
     * the dirty bit. This is required when the MMIO region falls in a
     * write-back cacheable PMA window (as 0x6000B000 does on NOEL-V). */
    _cbo_flush((void *)(uintptr_t)addr);
}

static inline uint32_t _mmio_read(const volatile uint32_t *addr)
{
    /* cbo.inval discards any stale cached copy so the read hits the device. */
    _cbo_inval((void *)(uintptr_t)addr);
    return *addr;
}

/* -------------------------------------------------------------------------
 * MDIO helpers
 *
 * Cadence GEM MDIO: write transaction to phy_maint register, then poll
 * net_status bit[2] (PHY_IDLE) until it goes 1 (idle = done).
 * ------------------------------------------------------------------------- */

/* GEM MMIO region (0x6000B000) may be in a cacheable PMA window on NOEL-V.
 * Invalidate the cache line before reading any GEM status/result register to
 * guarantee we fetch fresh data from the AXI bus rather than a stale hit. */
static void _mdio_wait(gem_regs_t *regs)
{
    for (unsigned i = 0; i < GEM_MDIO_TIMEOUT; i++) {
        if (_mmio_read(&regs->net_status) & GEM_NWSTAT_PHY_IDLE) {
            return;
        }
    }
    DEBUG("[gem] WARNING: MDIO timeout waiting for idle\n");
}

static int _mdio_read(gem_regs_t *regs, unsigned phy_addr, unsigned reg)
{
    _mdio_wait(regs);
    _mmio_write(&regs->phy_maint, GEM_MDIO_READ(phy_addr, reg));

    /* MDIO frame = 32 preamble + 32 data bits @ ~1.5 MHz MDC = ~43 µs.
     * Busy-wait then invalidate phy_maint cache line before reading result. */
    for (volatile unsigned i = 0; i < GEM_MDIO_TIMEOUT; i++) {}

    return (int)(_mmio_read(&regs->phy_maint) & 0xFFFFu);
}

static void _mdio_write(gem_regs_t *regs, unsigned phy_addr,
                        unsigned reg, uint16_t data)
{
    _mdio_wait(regs);
    _mmio_write(&regs->phy_maint, GEM_MDIO_WRITE(phy_addr, reg, data));
    _mdio_wait(regs);
}

/* -------------------------------------------------------------------------
 * PHY management
 * ------------------------------------------------------------------------- */

/* Ensure 1000BASE-T FD is advertised and restart auto-negotiation.
 *
 * The SLCR GEM0_CLK_CTRL is configured by ps7_init for 125 MHz TX clock
 * (IO PLL 1000 MHz / 8 / 1), which requires Gigabit RGMII. If a previous
 * firmware run wrote GBT_CTRL to disable 1000 Mbps, the PHY will have
 * negotiated at 100 Mbps in the meantime. This function restores the
 * 1000 Mbps advertisement and restarts AN so the link comes up at 1 Gbps. */
static void _phy_start_gbit_aneg(gem_t *dev)
{
    gem_regs_t *regs = GEM_REGS(dev);

    int gbt = _mdio_read(regs, dev->phy_addr, GEM_MII_GBT_CTRL);
    if (gbt >= 0) {
        _mdio_write(regs, dev->phy_addr, GEM_MII_GBT_CTRL,
                    (uint16_t)((uint32_t)gbt |
                               GEM_MII_GBT_CTRL_1000FD | GEM_MII_GBT_CTRL_1000HD));
    }

    int ctrl = _mdio_read(regs, dev->phy_addr, GEM_MII_CTRL);
    if (ctrl >= 0) {
        _mdio_write(regs, dev->phy_addr, GEM_MII_CTRL,
                    (uint16_t)((uint32_t)ctrl |
                               GEM_MII_CTRL_ANEG_EN | GEM_MII_CTRL_RESTART_AN));
    }
    printf("[gem] AN restarted (1000BASE-T enabled)\n");
}

/* Poll MII_STATUS until auto-negotiation completes or timeout. */
static void _phy_wait_aneg(gem_t *dev)
{
    gem_regs_t *regs = GEM_REGS(dev);

    for (unsigned i = 0; i < GEM_ANEG_TIMEOUT; i++) {
        int s = _mdio_read(regs, dev->phy_addr, GEM_MII_STATUS);
        if (s >= 0 && ((uint32_t)s & GEM_MII_STAT_ANEG_DONE)) {
            printf("[gem] AN done (status=0x%04x)\n", (unsigned)s);
            return;
        }
    }
    printf("[gem] AN timeout\n");
}

static unsigned _phy_detect(gem_regs_t *regs)
{
    for (unsigned i = 0; i < 32u; i++) {
        int id1 = _mdio_read(regs, i, GEM_MII_PHYSID1);
        if (id1 > 0 && id1 != 0xFFFF) {
            int id2    = _mdio_read(regs, i, GEM_MII_PHYSID2);
            int status = _mdio_read(regs, i, GEM_MII_STATUS);
            printf("[gem] PHY found at addr %u: id1=0x%04x id2=0x%04x status=0x%04x\n",
                   i, (unsigned)id1, (unsigned)id2, (unsigned)status);
            return i;
        }
    }
    printf("[gem] WARNING: no PHY found via MDIO\n");
    return 0;
}


/* -------------------------------------------------------------------------
 * MAC address helpers
 * ------------------------------------------------------------------------- */

static void _set_mac(gem_t *dev)
{
    gem_regs_t *regs = GEM_REGS(dev);
    const uint8_t *m = dev->params->mac;

    /* spec_addr1_bot: bytes [3:0] = mac[0..3], bot register = lower address bytes */
    _mmio_write(&regs->spec_addr1_bot,
                ((uint32_t)m[3] << 24) | ((uint32_t)m[2] << 16) |
                ((uint32_t)m[1] <<  8) |  (uint32_t)m[0]);
    /* spec_addr1_top: bytes [5:4] = mac[4..5] */
    _mmio_write(&regs->spec_addr1_top,
                ((uint32_t)m[5] << 8) | (uint32_t)m[4]);
}

/* -------------------------------------------------------------------------
 * Descriptor ring initialisation
 * ------------------------------------------------------------------------- */

static void _init_rx_ring(gem_t *dev)
{
    (void)dev;
    for (unsigned i = 0; i < CONFIG_GEM_RX_DESC_NUM; i++) {
        uint32_t ps_buf = GEM_TO_PS_PHYS(_rx_buf[i]);
        uint32_t addr   = ps_buf & ~3u;   /* clear control bits, keep address */
        if (i == CONFIG_GEM_RX_DESC_NUM - 1u) {
            addr |= GEM_RX_WRAP;           /* mark last descriptor */
        }
        /* GEM_RX_USED = 0: GEM owns the descriptor */
        _rx_desc[i].addr   = addr;
        _rx_desc[i].status = 0;
    }
    _dcache_flush_range(_rx_desc, sizeof(_rx_desc));
}

static void _init_tx_ring(gem_t *dev)
{
    (void)dev;
    /* GEM always restarts from tx_qbar after each START_TX.
     * Use a single descriptor with WRAP so GEM sends it, wraps back,
     * sees used=1 on re-entry, and stops cleanly. */
    _tx_desc[0].addr = 0;
    _tx_desc[0].ctrl = GEM_TX_USED | GEM_TX_WRAP;
    _dcache_flush_range(_tx_desc, sizeof(_tx_desc));
}

/* -------------------------------------------------------------------------
 * RX polling thread
 *
 * Checks the current RX descriptor every 1 ms. Posts NETDEV_EVENT_ISR to
 * the netif thread when a frame arrives (GEM_RX_USED set = CPU owns frame).
 * The actual data copy happens later in recv() called by the netif thread.
 * ------------------------------------------------------------------------- */

static void *_rx_poll_thread(void *arg)
{
    gem_t *dev = arg;

    while (1) {
        ztimer_sleep(ZTIMER_MSEC, 1);

        /* Scan entire ring: fire ISR once if any descriptor is ready */
        for (unsigned i = 0; i < CONFIG_GEM_RX_DESC_NUM; i++) {
            unsigned idx = (dev->rx_idx + i) % CONFIG_GEM_RX_DESC_NUM;
            _dcache_inval_range(&dev->rx_desc[idx], sizeof(gem_rx_desc_t));
            if (dev->rx_desc[idx].addr & GEM_RX_USED) {
                dev->netdev.event_callback(&dev->netdev, NETDEV_EVENT_ISR);
                break;
            }
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * netdev callbacks
 * ------------------------------------------------------------------------- */

static int _init(netdev_t *netdev)
{
    gem_t *dev = (gem_t *)netdev;
    gem_regs_t *regs = GEM_REGS(dev);

    dev->rx_desc = _rx_desc;
    dev->tx_desc = _tx_desc;
    dev->tx_buf  = _tx_buf;
    dev->rx_idx  = 0;
    dev->tx_idx  = 0;

    /* Disable TX/RX, clear all interrupts */
    _mmio_write(&regs->net_ctrl, 0);
    _mmio_write(&regs->intr_dis, 0xFFFFFFFFu);
    _mmio_write(&regs->tx_status, 0xFFFFFFFFu);
    _mmio_write(&regs->rx_status, 0xFFFFFFFFu);
    _mmio_write(&regs->intr_status, 0xFFFFFFFFu);

    /* Enable MDIO management port */
    _mmio_write(&regs->net_ctrl, GEM_NWCTRL_MDIO_EN);

    /* Detect PHY */
    dev->phy_addr = _phy_detect(regs);

    /* Re-enable 1000BASE-T advertisement and restart auto-negotiation.
     * The SLCR GEM0_CLK_CTRL is fixed at 125 MHz (IO PLL 1000 MHz / 8 / 1)
     * by ps7_init, so we must operate at 1000 Mbps. */
    _phy_start_gbit_aneg(dev);

    /* DMA config: INCR16 bursts, 1536B RX buffers (24 × 64 = 1536) */
    _mmio_write(&regs->dma_cfg, GEM_DMACFG_BURST_INCR16 | GEM_DMACFG_RXBUF(24u));

    /* Configure GEM for 1000 Mbps full-duplex (matches SLCR 125 MHz TX clock) */
    uint32_t net_cfg = GEM_NWCFG_MDC_DIV64 | GEM_NWCFG_GIGABIT | GEM_NWCFG_FULL_DUPLEX;
    _mmio_write(&regs->net_cfg, net_cfg);

    /* Set MAC address */
    _set_mac(dev);

    /* Initialise descriptor rings */
    _init_rx_ring(dev);
    _init_tx_ring(dev);

    /* Program descriptor ring base addresses (PS DDR physical addresses) */
    _mmio_write(&regs->rx_qbar, GEM_TO_PS_PHYS(_rx_desc));
    _mmio_write(&regs->tx_qbar, GEM_TO_PS_PHYS(_tx_desc));

    printf("[gem] rx_qbar=0x%08" PRIx32 " tx_qbar=0x%08" PRIx32 "\n",
           GEM_TO_PS_PHYS(_rx_desc), GEM_TO_PS_PHYS(_tx_desc));

    /* Enable TX and RX */
    _mmio_write(&regs->net_ctrl,
                GEM_NWCTRL_MDIO_EN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN);

    /* Wait for auto-negotiation to complete before releasing to GNRC */
    _phy_wait_aneg(dev);

    /* Launch RX polling thread */
    _gem_dev_ptr = dev;
    thread_create(_rx_poll_stack, sizeof(_rx_poll_stack),
                  THREAD_PRIORITY_MAIN - 1,
                  THREAD_CREATE_STACKTEST,
                  _rx_poll_thread, dev, "gem_rx_poll");

    /* Signal link up */
    netdev->event_callback(netdev, NETDEV_EVENT_LINK_UP);

    printf("[gem] net_cfg=0x%08" PRIx32 " net_ctrl=0x%08" PRIx32
           " rx_status=0x%08" PRIx32 " net_status=0x%08" PRIx32 "\n",
           _mmio_read(&regs->net_cfg),
           _mmio_read(&regs->net_ctrl),
           _mmio_read(&regs->rx_status),
           _mmio_read(&regs->net_status));
    printf("[gem] spec_addr1: bot=0x%08" PRIx32 " top=0x%08" PRIx32 "\n",
           _mmio_read(&regs->spec_addr1_bot),
           _mmio_read(&regs->spec_addr1_top));
    printf("[gem] init done: phy_addr=%u\n", dev->phy_addr);
    return 0;
}

static int _send(netdev_t *netdev, const iolist_t *iolist)
{
    gem_t *dev = (gem_t *)netdev;
    gem_regs_t *regs = GEM_REGS(dev);

    /* Flatten scatter-gather list into flat TX buffer */
    size_t len = 0;
    for (const iolist_t *il = iolist; il != NULL; il = il->iol_next) {
        if (len + il->iol_len > GEM_BUF_SIZE) {
            return -ENOBUFS;
        }
        memcpy(_tx_buf + len, il->iol_base, il->iol_len);
        len += il->iol_len;
    }

    /* Pad to minimum Ethernet frame size */
    if (len < ETHERNET_MIN_LEN) {
        memset(_tx_buf + len, 0, ETHERNET_MIN_LEN - len);
        len = ETHERNET_MIN_LEN;
    }

    /* Always use descriptor 0 with WRAP: GEM re-scans from tx_qbar on every
     * START_TX, so desc[0] must always be the active slot. */
    gem_tx_desc_t *desc = &dev->tx_desc[0];

    /* Wait for GEM to release the descriptor (used=1 → CPU owns) */
    for (unsigned i = 0; i < 100000u; i++) {
        _dcache_inval_range(desc, sizeof(gem_tx_desc_t));
        if (desc->ctrl & GEM_TX_USED) {
            break;
        }
        if (i == 99999u) {
            DEBUG("[gem] TX descriptor timeout\n");
            return -EBUSY;
        }
    }

    /* Write buffer address (PS DDR physical) */
    desc->addr = GEM_TO_PS_PHYS(_tx_buf);

    /* WRAP must stay set so GEM wraps after this descriptor and stops. */
    uint32_t ctrl = (uint32_t)(len & GEM_TX_LEN_MASK) | GEM_TX_LAST_BUF | GEM_TX_WRAP;

    /* Flush TX data buffer to PS DDR before GEM DMA reads it */
    _dcache_flush_range(_tx_buf, len);

    /* Write control word with used=0 to hand descriptor to GEM */
    desc->ctrl = ctrl;  /* used=0: GEM now owns */

    /* Flush descriptor to PS DDR */
    _dcache_flush_range(desc, sizeof(gem_tx_desc_t));
    __asm__ volatile("fence ow, ow" ::: "memory");

    /* Clear any stale TX status and start TX */
    _mmio_write(&regs->tx_status, 0xFFFFFFFFu);
    _mmio_write(&regs->net_ctrl,
                GEM_NWCTRL_MDIO_EN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN |
                GEM_NWCTRL_START_TX);

    printf("[gem] TX start: len=%u desc_addr=0x%08" PRIx32 " desc_ctrl=0x%08" PRIx32 "\n",
           (unsigned)len, desc->addr, desc->ctrl);

    /* Wait for TX complete: poll tx_status COMPLETE bit */
    bool done = false;
    uint32_t last_txst = 0;
    for (unsigned i = 0; i < 200000u; i++) {
        uint32_t txst = _mmio_read(&regs->tx_status);
        last_txst = txst;
        if (txst & GEM_TXSTAT_COMPLETE) {
            _mmio_write(&regs->tx_status, GEM_TXSTAT_COMPLETE);
            done = true;
            break;
        }
        if (txst & GEM_TXSTAT_ERR_MASK) {
            printf("[gem] TX error: tx_status=0x%08" PRIx32 "\n", txst);
            _mmio_write(&regs->tx_status, 0xFFFFFFFFu);
            return -EIO;
        }
    }

    printf("[gem] TX poll done=%d tx_status=0x%08" PRIx32 "\n", (int)done, last_txst);

    if (!done) {
        /* Fallback: check descriptor used bit */
        _dcache_inval_range(desc, sizeof(gem_tx_desc_t));
        done = !!(desc->ctrl & GEM_TX_USED);
        printf("[gem] TX fallback: desc_ctrl=0x%08" PRIx32 " done=%d\n",
               desc->ctrl, (int)done);
    }

    if (!done) {
        DEBUG("[gem] TX timeout\n");
        return -ETIMEDOUT;
    }

    return (int)len;
}

static int _recv(netdev_t *netdev, void *buf, size_t len, void *info)
{
    gem_t *dev = (gem_t *)netdev;

    (void)info;

    /* Scan from rx_idx over the full ring to find the first filled descriptor.
     * After a NO_BUF restart GEM restarts from rx_qbar (desc[0]) regardless
     * of rx_idx, so the filled descriptor may not be at rx_idx. */
    unsigned idx = CONFIG_GEM_RX_DESC_NUM;   /* sentinel = not found */
    for (unsigned i = 0; i < CONFIG_GEM_RX_DESC_NUM; i++) {
        unsigned ci = (dev->rx_idx + i) % CONFIG_GEM_RX_DESC_NUM;
        _dcache_inval_range(&dev->rx_desc[ci], sizeof(gem_rx_desc_t));
        if (dev->rx_desc[ci].addr & GEM_RX_USED) {
            idx = ci;
            break;
        }
    }

    if (idx == CONFIG_GEM_RX_DESC_NUM) {
        return 0;  /* no frame available */
    }

    gem_rx_desc_t *desc = &dev->rx_desc[idx];
    uint32_t status = desc->status;
    size_t frame_len = status & GEM_RX_STATUS_LEN_MASK;

    /* Query mode: caller passes buf=NULL, len=0 to get frame size only */
    if (buf == NULL && len == 0) {
        printf("[gem] RX: idx=%u len=%u\n", idx, (unsigned)frame_len);
        return (int)frame_len;
    }

    /* Check for RX errors (FCS, etc.) */
    if (status & GEM_RX_STATUS_FCS_ERR) {
        DEBUG("[gem] RX FCS error, dropping frame\n");
        frame_len = 0;
    }

    if (frame_len > 0 && buf != NULL) {
        if (frame_len > len) {
            frame_len = len;
        }
        /* Invalidate RX buffer from cache before CPU reads DMA-written data */
        _dcache_inval_range(_rx_buf[idx], frame_len);
        memcpy(buf, _rx_buf[idx], frame_len);
    }

    /* Re-arm descriptor: clear used bit to give back to GEM */
    uint32_t addr = GEM_TO_PS_PHYS(_rx_buf[idx]) & ~3u;
    if (idx == CONFIG_GEM_RX_DESC_NUM - 1u) {
        addr |= GEM_RX_WRAP;
    }
    desc->addr   = addr;  /* used=0 → GEM owns */
    desc->status = 0;
    _dcache_flush_range(desc, sizeof(gem_rx_desc_t));
    __asm__ volatile("fence ow, ow" ::: "memory");

    /* Write rxen after every re-arm: NOP when GEM is already running;
     * restarts from rx_qbar when GEM stalled due to NO_BUF. */
    gem_regs_t *regs = GEM_REGS(dev);
    _mmio_write(&regs->net_ctrl,
                GEM_NWCTRL_MDIO_EN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN);

    dev->rx_idx = (idx + 1u) % CONFIG_GEM_RX_DESC_NUM;
    return (int)frame_len;
}

static void _isr(netdev_t *netdev)
{
    gem_t *dev = (gem_t *)netdev;
    gem_regs_t *regs = GEM_REGS(dev);

    uint32_t rxst = _mmio_read(&regs->rx_status);
    uint32_t clear = rxst & (GEM_RXSTAT_FRAME_RECD | GEM_RXSTAT_OVERRUN |
                              GEM_RXSTAT_NO_BUF);
    if (clear) {
        _mmio_write(&regs->rx_status, clear);
    }

    if (rxst & GEM_RXSTAT_OVERRUN) {
        DEBUG("[gem] RX overrun\n");
    }

    /* Count ALL filled descriptors across the entire ring.
     * Do NOT break on the first empty slot: after a NO_BUF restart GEM goes
     * back to rx_qbar (desc[0]) regardless of rx_idx, so filled descriptors
     * can appear at arbitrary positions relative to rx_idx. */
    unsigned pending = 0;
    for (unsigned i = 0; i < CONFIG_GEM_RX_DESC_NUM; i++) {
        unsigned idx = (dev->rx_idx + i) % CONFIG_GEM_RX_DESC_NUM;
        _dcache_inval_range(&dev->rx_desc[idx], sizeof(gem_rx_desc_t));
        if (dev->rx_desc[idx].addr & GEM_RX_USED) {
            pending++;
        }
    }

    if (pending > 0) {
        printf("[gem] ISR: %u frame(s) pending\n", pending);
        for (unsigned i = 0; i < pending; i++) {
            netdev->event_callback(netdev, NETDEV_EVENT_RX_COMPLETE);
        }
    }
}

static int _get(netdev_t *netdev, netopt_t opt, void *val, size_t max_len)
{
    gem_t *dev = (gem_t *)netdev;

    if (opt == NETOPT_ADDRESS) {
        assert(max_len >= ETHERNET_ADDR_LEN);
        memcpy(val, dev->params->mac, ETHERNET_ADDR_LEN);
        return ETHERNET_ADDR_LEN;
    }
    return netdev_eth_get(netdev, opt, val, max_len);
}

static int _set(netdev_t *netdev, netopt_t opt, const void *val, size_t val_len)
{
    gem_t *dev = (gem_t *)netdev;
    gem_regs_t *regs = GEM_REGS(dev);

    if (opt == NETOPT_PROMISCUOUSMODE) {
        assert(val_len >= sizeof(netopt_enable_t));
        uint32_t cfg = _mmio_read(&regs->net_cfg);
        if (*(const netopt_enable_t *)val == NETOPT_ENABLE) {
            cfg |= GEM_NWCFG_COPY_ALL;
        }
        else {
            cfg &= ~GEM_NWCFG_COPY_ALL;
        }
        _mmio_write(&regs->net_cfg, cfg);
        return sizeof(netopt_enable_t);
    }
    return netdev_eth_set(netdev, opt, val, val_len);
}

static const netdev_driver_t _gem_driver = {
    .init         = _init,
    .send         = _send,
    .recv         = _recv,
    .isr          = _isr,
    .get          = _get,
    .set          = _set,
    .confirm_send = NULL,
};

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void gem_setup(gem_t *dev, const gem_params_t *params, uint8_t index)
{
    dev->params = params;
    dev->netdev.driver = &_gem_driver;
    netdev_register(&dev->netdev, NETDEV_ANY, index);
}
