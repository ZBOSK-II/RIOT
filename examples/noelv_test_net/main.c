/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @brief   Cadence GEM (PS GEM0) Ethernet driver test for NOEL-V / ZedBoard
 *
 * Manually initialises the Cadence GEM netdev and attaches it to the GNRC
 * network stack without relying on the auto_init system.
 *
 * Hardware path: NOEL-V (PL) → S_AXI_GP0 → PS GEM0 → Marvell 88E1518 PHY
 *   NOEL-V alias address 0x6000_B000 → PS GEM0 registers 0xE000_B000
 *   DMA buffers: NOEL-V phys → PS DDR phys via GEM_TO_PS_PHYS()
 *
 * What this example tests:
 *  1. Cadence GEM hardware init (PHY discovery, MDIO, auto-negotiation)
 *  2. GNRC Ethernet netif creation and IPv6 link-local address assignment
 *  3. Shell: ifconfig, ping6
 *
 * Usage (after flashing):
 *   > ifconfig          -- shows interface with link-local IPv6 addr
 *   > ping6 <addr>      -- ping another IPv6 host
 */

#include <stdio.h>

#include "msg.h"
#include "shell.h"
#include "thread.h"
#include "net/gnrc/netif/ethernet.h"
#include "net/gnrc/netif.h"

#include "cadence_gem.h"
#include "cadence_gem_params.h"

/* -------------------------------------------------------------------------
 * Static storage for the GEM device and its GNRC netif thread
 * ---------------------------------------------------------------------- */

#define GEM_STACKSIZE   (THREAD_STACKSIZE_DEFAULT)

static gem_t _gem_dev;
static gnrc_netif_t _gem_netif;
static char _gem_stack[GEM_STACKSIZE];

/* Message queue for the main/shell thread */
#define MAIN_MSG_QUEUE_SIZE (8)
static msg_t _main_msg_queue[MAIN_MSG_QUEUE_SIZE];

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    msg_init_queue(_main_msg_queue, MAIN_MSG_QUEUE_SIZE);

    puts("\r\n=== NOEL-V Cadence GEM (PS GEM0) Ethernet test ===\r\n");
    puts("Hardware path: NOEL-V 0x6000_B000 -> PS 0xE000_B000 (GEM0)\r\n");

    /* Step 1: wire the gem_t to the driver vtable and board params */
    gem_setup(&_gem_dev, &gem_params[0], 0);

    /* Step 2: create GNRC Ethernet netif thread
     *
     * gnrc_netif_ethernet_create() calls dev->init() which:
     *  - disables TX/RX, clears interrupts
     *  - enables MDIO and detects PHY
     *  - configures net_cfg (speed/duplex from PHY negotiation)
     *  - initialises RX/TX descriptor rings with PS DDR addresses
     *  - enables TX/RX in net_ctrl
     *  - spawns RX polling thread (checks RX descriptors every 1ms)
     */
    gnrc_netif_ethernet_create(&_gem_netif,
                               _gem_stack, GEM_STACKSIZE,
                               GNRC_NETIF_PRIO,
                               "gem0",
                               &_gem_dev.netdev);

    puts("GEM0 netif created. Running shell — type 'help' for commands.\r\n");
    puts("Useful commands:");
    puts("  ifconfig          — show interface and IPv6 address");
    puts("  ping6 <ipv6>      — send ICMPv6 echo request");
    puts("");

    /* Step 3: hand control to the interactive shell */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
