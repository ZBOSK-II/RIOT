/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @file
 * @brief   UDP throughput benchmark for NOEL-V GRETH Ethernet
 *
 * RX benchmark (laptop → board):
 *   Laptop: python3 udp_flood_tx.py <board-link-local>
 *   Board prints MB/s once per second automatically on port 8888.
 *
 * TX benchmark (board → laptop):
 *   Laptop: python3 udp_bench_rx.py          (listens on UDP 8888)
 *   Board:  udpflood <laptop-link-local> [seconds=5]
 *
 * Commands: ifconfig | ping6 <addr> | udpflood <addr> [secs]
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "msg.h"
#include "shell.h"
#include "thread.h"
#include "ztimer.h"
#include "net/gnrc/netif.h"
#include "net/sock/udp.h"
#include "net/ipv6/addr.h"


#define BENCH_PORT       (8888U)
#define BENCH_BUF_SIZE   (1500U)
#define BENCH_PERIOD_US  (1000000UL)

static char    _bench_rx_stack[THREAD_STACKSIZE_DEFAULT + 512];
static uint8_t _bench_rx_buf[BENCH_BUF_SIZE];

static void *_bench_rx_thread(void *arg)
{
    (void)arg;

    sock_udp_t sock;
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = BENCH_PORT;

    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("[bench-rx] ERROR: socket create failed");
        return NULL;
    }
    puts("[bench-rx] UDP sink on port 8888");

    uint64_t rx_bytes = 0;
    uint32_t rx_pkts  = 0;
    uint32_t t0 = ztimer_now(ZTIMER_USEC);

    while (1) {
        sock_udp_ep_t remote;
        ssize_t res = sock_udp_recv(&sock, _bench_rx_buf, sizeof(_bench_rx_buf),
                                    SOCK_NO_TIMEOUT, &remote);
        if (res < 0) {
            continue;
        }
        rx_bytes += (uint64_t)res;
        rx_pkts++;

        uint32_t now     = ztimer_now(ZTIMER_USEC);
        uint32_t elapsed = now - t0;
        if (elapsed >= BENCH_PERIOD_US) {
            /* mbs_x10 = (MB/s) * 10  — avoids floating point */
            uint32_t mbs_x10 = (uint32_t)((rx_bytes * 10ULL) / elapsed);
            uint32_t mbps    = (mbs_x10 * 8U) / 10U;
            printf("[bench-rx] %5" PRIu32 " pkts  %3" PRIu32 ".%1" PRIu32 " MB/s  ~%3" PRIu32 " Mbps\n",
                   rx_pkts, mbs_x10 / 10U, mbs_x10 % 10U, mbps);
            rx_bytes = 0;
            rx_pkts  = 0;
            t0       = now;
        }
    }
    return NULL;
}


static uint8_t _flood_buf[1400];

static int _cmd_udpflood(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <ipv6-addr> [seconds=5]\n"
               "  Floods 1400-byte UDP to <addr>:%u, prints TX MB/s per second.\n"
               "  Run 'python3 udp_bench_rx.py' on laptop to count received bytes.\n",
               argv[0], BENCH_PORT);
        return 1;
    }

    uint32_t duration_s = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 5U;

    char addr_str[64];
    strncpy(addr_str, argv[1], sizeof(addr_str) - 1);
    addr_str[sizeof(addr_str) - 1] = '\0';
    char *pct = strchr(addr_str, '%');
    if (pct) {
        *pct = '\0';
    }

    ipv6_addr_t addr;
    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        printf("Error: invalid IPv6 address '%s'\n", argv[1]);
        return 1;
    }

    sock_udp_ep_t remote = { .family = AF_INET6, .port = BENCH_PORT };
    memcpy(&remote.addr.ipv6, &addr, sizeof(addr));

    if (ipv6_addr_is_link_local(&addr)) {
        gnrc_netif_t *netif = gnrc_netif_iter(NULL);
        if (netif) {
            remote.netif = (uint16_t)netif->pid;
        }
    }

    sock_udp_t sock;
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = 9987U;

    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("Error: socket create failed");
        return 1;
    }

    memset(_flood_buf, 0xAA, sizeof(_flood_buf));
    printf("[flood] TX → %s:%u for %" PRIu32 "s\n", argv[1], BENCH_PORT, duration_s);

    uint32_t t_start  = ztimer_now(ZTIMER_USEC);
    uint32_t t_end    = t_start + duration_s * 1000000UL;
    uint32_t t_stat   = t_start + BENCH_PERIOD_US;
    uint64_t tx_total = 0;
    uint64_t tx_per   = 0;
    uint32_t tx_err   = 0;

    while (1) {
        uint32_t now = ztimer_now(ZTIMER_USEC);
        if ((int32_t)(now - t_end) >= 0) {
            break;
        }

        ssize_t res = sock_udp_send(&sock, _flood_buf, sizeof(_flood_buf), &remote);
        if (res > 0) {
            tx_total += (uint64_t)res;
            tx_per   += (uint64_t)res;
        }
        else {
            tx_err++;
        }

        if ((int32_t)(now - t_stat) >= 0) {
            uint32_t elapsed = now - (t_stat - BENCH_PERIOD_US);
            uint32_t mbs_x10 = (elapsed > 0U) ? (uint32_t)((tx_per * 10ULL) / elapsed) : 0U;
            uint32_t mbps    = (mbs_x10 * 8U) / 10U;
            printf("[flood] TX: %3" PRIu32 ".%1" PRIu32 " MB/s  ~%3" PRIu32 " Mbps  err:%" PRIu32 "\n",
                   mbs_x10 / 10U, mbs_x10 % 10U, mbps, tx_err);
            tx_per  = 0;
            tx_err  = 0;
            t_stat += BENCH_PERIOD_US;
        }
    }

    uint32_t total_us = ztimer_now(ZTIMER_USEC) - t_start;
    uint32_t avg_x10  = (total_us > 0U) ? (uint32_t)((tx_total * 10ULL) / total_us) : 0U;
    printf("[flood] done: %" PRIu32 " KB  avg %3" PRIu32 ".%1" PRIu32 " MB/s  ~%3" PRIu32 " Mbps\n",
           (uint32_t)(tx_total / 1024UL),
           avg_x10 / 10U, avg_x10 % 10U, (avg_x10 * 8U) / 10U);

    sock_udp_close(&sock);
    return 0;
}

static const shell_command_t _shell_cmds[] = {
    { "udpflood", "TX flood: udpflood <ipv6-addr> [secs=5]", _cmd_udpflood },
    { NULL, NULL, NULL }
};

#define MAIN_MSG_QUEUE_SIZE (8)
static msg_t _msg_queue[MAIN_MSG_QUEUE_SIZE];

int main(void)
{
    msg_init_queue(_msg_queue, MAIN_MSG_QUEUE_SIZE);
    puts("=== NOEL-V GRETH UDP throughput benchmark ===");


    thread_create(_bench_rx_stack, sizeof(_bench_rx_stack),
                  THREAD_PRIORITY_MAIN - 1,
                  THREAD_CREATE_STACKTEST,
                  _bench_rx_thread, NULL, "bench_rx");

    puts("greth0 up.");
    puts("  RX bench: flood UDP 8888 from laptop -> board prints MB/s");
    puts("  TX bench: udpflood <laptop-addr> [secs]");
    puts("Commands: ifconfig | ping6 | udpflood");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(_shell_cmds, line_buf, SHELL_DEFAULT_BUFSIZE);
    return 0;
}
