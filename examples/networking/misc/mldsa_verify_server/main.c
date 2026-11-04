/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       TCP server that verifies ML-DSA-44 signatures
 *
 * Listens on an IPv6 TCP port, receives `message + signature` requests,
 * verifies them against the public key compiled into the firmware and
 * answers with a status and the time the verification took. The outcome is
 * also shown on the LEDs and the console.
 *
 * @author      Matvii Ivashchenko
 *
 * @}
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "byteorder.h"
#include "led.h"
#include "mldsa_pubkey.h"
#include "mldsa_testvec.h"
#include "net/af.h"
#include "net/gnrc/tcp.h"
#include "net/ipv6/addr.h"
#include "net/netif.h"
#include "pqclean_mldsa.h"
#include "shell.h"
#include "thread.h"
#include "ztimer.h"

/**
 * @name    Wire protocol
 *
 * Both directions start with a 12 byte header, all fields big-endian.
 *
 * Request:  magic "MLDS" | version u8 | alg u8 | flags u8 | reserved u8 |
 *           msg_len u16 | sig_len u16 | msg | sig
 * Response: magic "MLDS" | status u8 | reserved u8 | verify_time_us u32 |
 *           msg_len u16
 * @{
 */
#define MLDSA_MAGIC              "MLDS"
#define MLDSA_MAGIC_LEN          (4U)
#define MLDSA_PROTO_VERSION      (1U)
#define MLDSA_ALG_MLDSA44        (1U)
#define MLDSA_HDR_LEN            (12U)
#define MLDSA_RSP_LEN            (12U)

#define MLDSA_STATUS_OK          (0U)
#define MLDSA_STATUS_BAD_SIG     (1U)
#define MLDSA_STATUS_BAD_REQUEST (2U)
#define MLDSA_STATUS_UNSUPPORTED (3U)
/** @} */

#ifndef MLDSA_SERVER_PORT
#  define MLDSA_SERVER_PORT (7777U)
#endif
#ifndef MLDSA_SERVER_MAX_MSG_LEN
#  define MLDSA_SERVER_MAX_MSG_LEN (1024U)
#endif

/* how long a single recv/send may block before the connection is dropped */
#define MLDSA_SERVER_IO_TIMEOUT_MS    (5000U)
/* how long to wait for the client to close after the response was sent */
#define MLDSA_SERVER_CLOSE_TIMEOUT_MS (2000U)
/* verification keeps about 36 KiB of polynomial vectors on the stack */
#define MLDSA_SERVER_STACKSIZE        (64 * 1024)

/* set to 0 to leave the LEDs alone */
#ifndef MLDSA_SERVER_USE_LEDS
#  define MLDSA_SERVER_USE_LEDS (1)
#endif
#if MLDSA_SERVER_USE_LEDS
#  define _led(op) op
#else
#  define _led(op) (void)0
#endif

typedef struct {
    uint32_t requests;
    uint32_t ok;
    uint32_t bad_sig;
    uint32_t bad_req;
    uint32_t conn_err;
    uint32_t verify_min_us;
    uint32_t verify_max_us;
    uint32_t verify_sum_us;
} _stats_t;

static char _server_stack[MLDSA_SERVER_STACKSIZE];
static gnrc_tcp_tcb_t _tcb;
static gnrc_tcp_tcb_queue_t _queue = GNRC_TCP_TCB_QUEUE_INIT;
static uint8_t _req[MLDSA_HDR_LEN + MLDSA_SERVER_MAX_MSG_LEN + PQCLEAN_MLDSA_BYTES];
static _stats_t _stats = { .verify_min_us = UINT32_MAX };

static const char *_status_str(unsigned status)
{
    switch (status) {
    case MLDSA_STATUS_OK:
        return "OK";
    case MLDSA_STATUS_BAD_SIG:
        return "BAD_SIG";
    case MLDSA_STATUS_BAD_REQUEST:
        return "BAD_REQUEST";
    case MLDSA_STATUS_UNSUPPORTED:
        return "UNSUPPORTED";
    default:
        return "?";
    }
}

static void _led_result(bool ok)
{
    if (ok) {
        _led(LED2_ON);
        _led(LED3_OFF);
    }
    else {
        _led(LED3_ON);
        _led(LED2_OFF);
    }
}

static void _peer_str(gnrc_tcp_tcb_t *tcb, char *out, size_t len)
{
    gnrc_tcp_ep_t ep;
    ipv6_addr_t addr;

    if (gnrc_tcp_get_remote(tcb, &ep) != 0) {
        strncpy(out, "?", len);
        return;
    }
    memcpy(&addr, ep.addr.ipv6, sizeof(addr));
    ipv6_addr_to_str(out, &addr, len);
}

/* Receive exactly len bytes. TCP delivers the request in several segments. */
static int _recv_exact(gnrc_tcp_tcb_t *tcb, uint8_t *buf, size_t len)
{
    size_t got = 0;

    while (got < len) {
        ssize_t res = gnrc_tcp_recv(tcb, buf + got, len - got, MLDSA_SERVER_IO_TIMEOUT_MS);
        if (res == 0) {
            /* peer closed before the request was complete */
            return -ECONNRESET;
        }
        if (res < 0) {
            return (int)res;
        }
        got += (size_t)res;
    }
    return 0;
}

/* gnrc_tcp_send() transmits one segment per call, so loop until done. */
static int _send_all(gnrc_tcp_tcb_t *tcb, const uint8_t *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t res = gnrc_tcp_send(tcb, buf + sent, len - sent, MLDSA_SERVER_IO_TIMEOUT_MS);
        if (res == 0) {
            return -ETIMEDOUT;
        }
        if (res < 0) {
            return (int)res;
        }
        sent += (size_t)res;
    }
    return 0;
}

static unsigned _parse_header(const uint8_t *hdr, uint16_t *msg_len, uint16_t *sig_len)
{
    if (memcmp(hdr, MLDSA_MAGIC, MLDSA_MAGIC_LEN) != 0) {
        return MLDSA_STATUS_BAD_REQUEST;
    }
    if ((hdr[4] != MLDSA_PROTO_VERSION) || (hdr[5] != MLDSA_ALG_MLDSA44)) {
        return MLDSA_STATUS_UNSUPPORTED;
    }
    *msg_len = byteorder_bebuftohs(hdr + 8);
    *sig_len = byteorder_bebuftohs(hdr + 10);
    if ((*msg_len > MLDSA_SERVER_MAX_MSG_LEN) || (*sig_len != PQCLEAN_MLDSA_BYTES)) {
        return MLDSA_STATUS_BAD_REQUEST;
    }
    return MLDSA_STATUS_OK;
}

static void _build_response(uint8_t *rsp, unsigned status, uint32_t verify_us, uint16_t msg_len)
{
    memcpy(rsp, MLDSA_MAGIC, MLDSA_MAGIC_LEN);
    rsp[4] = (uint8_t)status;
    rsp[5] = 0;
    byteorder_htobebufl(rsp + 6, verify_us);
    byteorder_htobebufs(rsp + 10, msg_len);
}

static void _stats_add_verify(uint32_t verify_us)
{
    if (verify_us < _stats.verify_min_us) {
        _stats.verify_min_us = verify_us;
    }
    if (verify_us > _stats.verify_max_us) {
        _stats.verify_max_us = verify_us;
    }
    _stats.verify_sum_us += verify_us;
}

/* The client closes first. Drain whatever it still sent, then wait for its
 * FIN: a graceful close keeps the single connection slot out of TIME_WAIT and
 * leaves no stale bytes in the receive buffer for the next connection. */
static void _finish_connection(gnrc_tcp_tcb_t *tcb)
{
    uint8_t scratch[64];
    ssize_t res;

    do {
        res = gnrc_tcp_recv(tcb, scratch, sizeof(scratch), MLDSA_SERVER_CLOSE_TIMEOUT_MS);
    } while (res > 0);

    if (res == 0) {
        gnrc_tcp_close(tcb);
    }
    else {
        gnrc_tcp_abort(tcb);
    }
}

static void _handle_connection(gnrc_tcp_tcb_t *tcb)
{
    char peer[IPV6_ADDR_MAX_STR_LEN];
    uint8_t rsp[MLDSA_RSP_LEN];
    uint16_t msg_len = 0;
    uint16_t sig_len = 0;
    uint32_t t_start = ztimer_now(ZTIMER_USEC);
    unsigned n = ++_stats.requests;

    _led(LED0_TOGGLE);
    _peer_str(tcb, peer, sizeof(peer));

    int res = _recv_exact(tcb, _req, MLDSA_HDR_LEN);
    if (res < 0) {
        printf("[#%u] %s: header: %s\n", n, peer, strerror(-res));
        _stats.conn_err++;
        gnrc_tcp_abort(tcb);
        return;
    }

    unsigned status = _parse_header(_req, &msg_len, &sig_len);
    if (status != MLDSA_STATUS_OK) {
        _build_response(rsp, status, 0, 0);
        _send_all(tcb, rsp, sizeof(rsp));
        printf("[#%u] %s: %s\n", n, peer, _status_str(status));
        _stats.bad_req++;
        _led_result(false);
        _finish_connection(tcb);
        return;
    }

    res = _recv_exact(tcb, _req + MLDSA_HDR_LEN, (size_t)msg_len + sig_len);
    if (res < 0) {
        printf("[#%u] %s: payload: %s\n", n, peer, strerror(-res));
        _stats.conn_err++;
        gnrc_tcp_abort(tcb);
        return;
    }

    const uint8_t *msg = _req + MLDSA_HDR_LEN;
    const uint8_t *sig = msg + msg_len;

    _led(LED1_ON);
    uint32_t t_verify = ztimer_now(ZTIMER_USEC);
    bool valid = (pqclean_mldsa_verify(sig, sig_len, msg, msg_len, mldsa_pk) == 0);
    uint32_t verify_us = ztimer_now(ZTIMER_USEC) - t_verify;
    _led(LED1_OFF);

    _led_result(valid);
    status = valid ? MLDSA_STATUS_OK : MLDSA_STATUS_BAD_SIG;
    _build_response(rsp, status, verify_us, msg_len);
    res = _send_all(tcb, rsp, sizeof(rsp));
    if (res < 0) {
        printf("[#%u] %s: response: %s\n", n, peer, strerror(-res));
        _stats.conn_err++;
        gnrc_tcp_abort(tcb);
        return;
    }

    _finish_connection(tcb);

    uint32_t total_us = ztimer_now(ZTIMER_USEC) - t_start;
    if (valid) {
        _stats.ok++;
    }
    else {
        _stats.bad_sig++;
    }
    _stats_add_verify(verify_us);
    printf("[#%u] %s msg_len=%u result=%s verify=%" PRIu32 " us total=%" PRIu32 " us\n", n, peer,
           msg_len, _status_str(status), verify_us, total_us);
}

static void *_server_thread(void *arg)
{
    (void)arg;
    gnrc_tcp_ep_t local;

    gnrc_tcp_ep_init(&local, AF_INET6, NULL, 0, MLDSA_SERVER_PORT, 0);
    gnrc_tcp_tcb_init(&_tcb);

    int res = gnrc_tcp_listen(&_queue, &_tcb, 1, &local);
    if (res < 0) {
        printf("listen failed: %s\n", strerror(-res));
        return NULL;
    }
    printf("listening on [::]:%u\n", MLDSA_SERVER_PORT);
    _led(LED0_ON);

    ztimer_acquire(ZTIMER_USEC);
    while (1) {
        gnrc_tcp_tcb_t *tcb = NULL;

        res = gnrc_tcp_accept(&_queue, &tcb, GNRC_TCP_NO_TIMEOUT);
        if (res < 0) {
            printf("accept failed: %s\n", strerror(-res));
            _stats.conn_err++;
            ztimer_sleep(ZTIMER_USEC, 100 * US_PER_MS);
            continue;
        }
        _handle_connection(tcb);
    }
    return NULL;
}

static int _cmd_stats(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t verified = _stats.ok + _stats.bad_sig;

    printf("requests %" PRIu32 ": ok %" PRIu32 ", bad signature %" PRIu32 ", bad request %" PRIu32
           ", connection errors %" PRIu32 "\n",
           _stats.requests, _stats.ok, _stats.bad_sig, _stats.bad_req, _stats.conn_err);
    if (verified > 0) {
        printf("verify time: min %" PRIu32 " us, avg %" PRIu32 " us, max %" PRIu32 " us\n",
               _stats.verify_min_us, _stats.verify_sum_us / verified, _stats.verify_max_us);
    }
    return 0;
}
SHELL_COMMAND(mldsa_stats, "ML-DSA server statistics", _cmd_stats);

static int _cmd_selftest(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t sig[PQCLEAN_MLDSA_BYTES];
    bool ok = true;

    memcpy(sig, mldsa_test_sig, sizeof(sig));
    ztimer_acquire(ZTIMER_USEC);

    uint32_t t0 = ztimer_now(ZTIMER_USEC);
    int res = pqclean_mldsa_verify(sig, sizeof(sig), mldsa_test_msg, MLDSA_TEST_MSG_LEN, mldsa_pk);
    uint32_t dt = ztimer_now(ZTIMER_USEC) - t0;
    printf("known-good signature: %s (%" PRIu32 " us)\n", (res == 0) ? "OK" : "REJECTED", dt);
    ok = ok && (res == 0);

    sig[sizeof(sig) / 2] ^= 0x01;
    t0 = ztimer_now(ZTIMER_USEC);
    res = pqclean_mldsa_verify(sig, sizeof(sig), mldsa_test_msg, MLDSA_TEST_MSG_LEN, mldsa_pk);
    dt = ztimer_now(ZTIMER_USEC) - t0;
    printf("corrupted signature:  %s (%" PRIu32 " us)\n", (res == 0) ? "ACCEPTED" : "REJECTED", dt);
    ok = ok && (res != 0);

    ztimer_release(ZTIMER_USEC);
    puts(ok ? "self-test passed" : "self-test FAILED");
    return ok ? 0 : 1;
}
SHELL_COMMAND(mldsa_selftest, "verify the built-in test vector", _cmd_selftest);

int main(void)
{
    puts("ML-DSA-44 verify server (PQClean)");
    printf("public key: %02x%02x%02x%02x... (%u bytes)\n", mldsa_pk[0], mldsa_pk[1], mldsa_pk[2],
           mldsa_pk[3], (unsigned)sizeof(mldsa_pk));
    puts("addresses:");
    netifs_print_ipv6("\n");
    puts("");

    thread_create(_server_stack, sizeof(_server_stack), THREAD_PRIORITY_MAIN - 1, 0, _server_thread,
                  NULL, "mldsa_server");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);
    return 0;
}
