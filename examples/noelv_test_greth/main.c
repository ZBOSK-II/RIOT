/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @brief   GRETH Ethernet driver test for NOEL-V / ZedBoard
 *
 * @author  Matvii Ivashchenko
 *
 * Hardware path:
 *   NOEL-V → GRETH (AHB, base 0xff984000, IRQ 5) → PMOD-JD RMII → LAN8720A PHY
 *
 * Pin map (Bank 13, LVCMOS33):
 *   REFCLK ← JD4_N/U5   CRS_DV → JD4_P/U6
 *   RXD[0] → JD3_P/W6   RXD[1] → JD3_N/W5
 *   TXD[0] ← JD1_P/V7   TXD[1] ← JD1_N/W7
 *   TX_EN  ← JD2_P/V5
 *
 * Usage:
 *   > ifconfig               — show interface and link-local IPv6 address
 *   > ping6 <addr>           — ICMPv6 echo request
 *   > udpsend <addr> <msg>   — send UDP text to a host (port 8888)
 *
 * UDP echo server runs automatically on port 8888.
 * Any UDP packet received is printed and echoed back unchanged.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "msg.h"
#include "shell.h"
#include "thread.h"
#include "net/gnrc/netif/ethernet.h"
#include "net/gnrc/netif.h"
#include "net/sock/udp.h"
#include "net/ipv6/addr.h"

#include "greth.h"
#include "greth_params.h"

/* -------------------------------------------------------------------------
 * GRETH netif
 * ---------------------------------------------------------------------- */

#define GRETH_STACKSIZE (THREAD_STACKSIZE_DEFAULT)

static greth_t      _dev;
static gnrc_netif_t _netif;
static char         _greth_stack[GRETH_STACKSIZE];

/* -------------------------------------------------------------------------
 * UDP echo server
 * ---------------------------------------------------------------------- */

#define UDP_ECHO_PORT       (8888U)
#define UDP_BUF_SIZE        (512U)
#define UDP_ECHO_STACKSIZE  (THREAD_STACKSIZE_DEFAULT + 512)

static char _udp_stack[UDP_ECHO_STACKSIZE];

static void *_udp_echo_thread(void *arg)
{
    (void)arg;

    sock_udp_t sock;
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = UDP_ECHO_PORT;

    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("[udp] ERROR: failed to create socket");
        return NULL;
    }

    printf("[udp] echo server listening on port %u\n", UDP_ECHO_PORT);

    static uint8_t buf[UDP_BUF_SIZE];
    uint32_t count = 0;

    while (1) {
        sock_udp_ep_t remote;
        ssize_t res = sock_udp_recv(&sock, buf, sizeof(buf) - 1,
                                    SOCK_NO_TIMEOUT, &remote);
        if (res < 0) {
            printf("[udp] recv error: %d\n", (int)res);
            continue;
        }

        buf[res] = '\0';
        printf("[udp] #%" PRIu32 " recv %d bytes: \"%s\"\n",
               count++, (int)res, (char *)buf);

        ssize_t sent = sock_udp_send(&sock, buf, res, &remote);
        if (sent < 0) {
            printf("[udp] send error: %d\n", (int)sent);
        }
        else {
            printf("[udp] echoed %d bytes back\n", (int)sent);
        }
    }
    return NULL;
}

static int _cmd_udpsend(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s <ipv6-addr> <message>\n", argv[0]);
        puts("  Example: udpsend fe80::bd30:ef19:debd:e59a%%5 'hello'");
        return 1;
    }

    ipv6_addr_t addr;
    char addr_str[64];
    strncpy(addr_str, argv[1], sizeof(addr_str) - 1);
    addr_str[sizeof(addr_str) - 1] = '\0';
    char *pct = strchr(addr_str, '%');
    if (pct) {
        *pct = '\0';
    }

    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        printf("Error: invalid IPv6 address '%s'\n", argv[1]);
        return 1;
    }

    sock_udp_ep_t remote = { .family = AF_INET6, .port = UDP_ECHO_PORT };
    memcpy(&remote.addr.ipv6, &addr, sizeof(addr));

    if (ipv6_addr_is_link_local(&addr)) {
        gnrc_netif_t *netif = gnrc_netif_iter(NULL);
        if (netif) {
            remote.netif = (uint16_t)netif->pid;
        }
    }

    sock_udp_t sock;
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = 9988; /* fixed port so GNRC registers listener before send */

    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("Error: failed to create UDP socket");
        return 1;
    }

    const char *msg = argv[2];
    ssize_t res = sock_udp_send(&sock, msg, strlen(msg), &remote);
    if (res < 0) {
        printf("Error: send failed (%d)\n", (int)res);
    }
    else {
        printf("[udp] sent %d bytes to %s port %u\n",
               (int)res, argv[1], UDP_ECHO_PORT);

        uint8_t buf[UDP_BUF_SIZE];
        sock_udp_ep_t from;
        res = sock_udp_recv(&sock, buf, sizeof(buf) - 1, 2000000UL, &from);
        if (res < 0) {
            puts("[udp] no echo reply (timeout 2s)");
        }
        else {
            buf[res] = '\0';
            printf("[udp] echo reply %d bytes: \"%s\"\n", (int)res, (char *)buf);
        }
    }

    sock_udp_close(&sock);
    return 0;
}

static const shell_command_t _shell_cmds[] = {
    { "udpsend", "Send UDP text to host:8888 and print echo", _cmd_udpsend },
    { NULL, NULL, NULL }
};

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

#define MAIN_MSG_QUEUE_SIZE (8)
static msg_t _msg_queue[MAIN_MSG_QUEUE_SIZE];

int main(void)
{
    msg_init_queue(_msg_queue, MAIN_MSG_QUEUE_SIZE);

    puts("=== NOEL-V GRETH (PMOD-JD RMII) Ethernet test ===");

    greth_setup(&_dev, &greth_params[0], 0);

    gnrc_netif_ethernet_create(&_netif, _greth_stack, GRETH_STACKSIZE,
                               GNRC_NETIF_PRIO, "greth0",
                               &_dev.netdev);

    /* Start UDP echo server */
    thread_create(_udp_stack, sizeof(_udp_stack),
                  THREAD_PRIORITY_MAIN - 1,
                  THREAD_CREATE_STACKTEST,
                  _udp_echo_thread, NULL, "udp_echo");

    puts("greth0 up. UDP echo server on port 8888.");
    puts("Commands: ifconfig | ping6 <addr> | udpsend <addr> <msg>");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(_shell_cmds, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
