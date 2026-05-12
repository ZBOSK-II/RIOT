/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @brief   GRETH Ethernet driver test for NOEL-V / ZedBoard
 *
 * Hardware path:
 *   NOEL-V → GRETH (AHB, base 0xff984000, IRQ 5) → PMOD-JD RMII → LAN8720A PHY
 *
 * Pin map (Bank 13, LVCMOS33):
 *   REFCLK ← JD4_N/U5   CRS_DV → JD4_P/U6
 *   RXD[0] → JD3_P/W6   RXD[1] → JD3_N/W5
 *   TXD[0] ← JD1_P/V7   TXD[1] ← JD1_N/W7
 *   TX_EN  ← JD2_P/V5   MDC    ← JD2_N/V4
 *   MDIO   ↔ JC2_P/Y4   nRST   ← JC2_N/AA4
 *
 * Usage:
 *   > ifconfig          — show interface and link-local IPv6 address
 *   > ping6 <addr>      — ICMPv6 echo request
 */

#include <stdio.h>

#include "msg.h"
#include "shell.h"
#include "net/gnrc/netif/ethernet.h"
#include "net/gnrc/netif.h"

#include "greth.h"
#include "greth_params.h"

#define STACK_SIZE  (THREAD_STACKSIZE_DEFAULT)

static greth_t      _dev;
static gnrc_netif_t _netif;
static char         _stack[STACK_SIZE];

#define MAIN_MSG_QUEUE_SIZE (8)
static msg_t _msg_queue[MAIN_MSG_QUEUE_SIZE];

int main(void)
{
    msg_init_queue(_msg_queue, MAIN_MSG_QUEUE_SIZE);

    puts("=== NOEL-V GRETH (PMOD-JD RMII) Ethernet test ===");

    greth_setup(&_dev, &greth_params[0], 0);

    gnrc_netif_ethernet_create(&_netif, _stack, STACK_SIZE,
                               GNRC_NETIF_PRIO, "greth0",
                               &_dev.netdev);

    puts("greth0 netif created. Type 'help' for commands.");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
