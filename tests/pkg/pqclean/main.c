/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Self-test and timing of the PQClean ML-DSA package
 *
 * Runs keypair / sign / verify for a few rounds, checks that a corrupted
 * signature and a different message are rejected, and prints the time each
 * operation took.
 *
 * @author      Matvii Ivashchenko
 *
 * @}
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pqclean_mldsa.h"
#include "ztimer.h"

#ifndef MLDSA_TEST_ROUNDS
#  define MLDSA_TEST_ROUNDS (5U)
#endif

typedef struct {
    uint32_t min;
    uint32_t max;
    uint32_t sum;
} _stat_t;

static uint8_t _pk[PQCLEAN_MLDSA_PUBLICKEYBYTES];
static uint8_t _sk[PQCLEAN_MLDSA_SECRETKEYBYTES];
static uint8_t _sig[PQCLEAN_MLDSA_BYTES];
static const uint8_t _msg[] = "NOEL-V ML-DSA self-test message";

static void _stat_add(_stat_t *s, uint32_t v)
{
    if (v < s->min) {
        s->min = v;
    }
    if (v > s->max) {
        s->max = v;
    }
    s->sum += v;
}

static void _stat_print(const char *name, const _stat_t *s, unsigned n)
{
    printf("%-7s min %" PRIu32 " us, avg %" PRIu32 " us, max %" PRIu32 " us\n", name, s->min,
           s->sum / n, s->max);
}

static bool _round(unsigned i, _stat_t *keygen, _stat_t *sign, _stat_t *verify)
{
    uint8_t msg2[sizeof(_msg)];
    size_t siglen = 0;

    uint32_t t0 = ztimer_now(ZTIMER_USEC);
    if (pqclean_mldsa_keypair(_pk, _sk) != 0) {
        puts("keypair failed");
        return false;
    }
    uint32_t t1 = ztimer_now(ZTIMER_USEC);
    if (pqclean_mldsa_sign(_sig, &siglen, _msg, sizeof(_msg), _sk) != 0) {
        puts("sign failed");
        return false;
    }
    if (siglen != PQCLEAN_MLDSA_BYTES) {
        printf("unexpected signature length %u\n", (unsigned)siglen);
        return false;
    }
    uint32_t t2 = ztimer_now(ZTIMER_USEC);
    int res = pqclean_mldsa_verify(_sig, siglen, _msg, sizeof(_msg), _pk);
    uint32_t t3 = ztimer_now(ZTIMER_USEC);
    if (res != 0) {
        puts("valid signature rejected");
        return false;
    }

    /* a corrupted signature must be rejected */
    _sig[i % siglen] ^= 0x01;
    if (pqclean_mldsa_verify(_sig, siglen, _msg, sizeof(_msg), _pk) == 0) {
        puts("corrupted signature accepted");
        return false;
    }
    _sig[i % siglen] ^= 0x01;

    /* a different message must be rejected */
    memcpy(msg2, _msg, sizeof(_msg));
    msg2[i % sizeof(msg2)] ^= 0x01;
    if (pqclean_mldsa_verify(_sig, siglen, msg2, sizeof(msg2), _pk) == 0) {
        puts("signature accepted for a different message");
        return false;
    }

    _stat_add(keygen, t1 - t0);
    _stat_add(sign, t2 - t1);
    _stat_add(verify, t3 - t2);
    printf("round %u: keygen %" PRIu32 " us, sign %" PRIu32 " us, verify %" PRIu32 " us\n", i,
           t1 - t0, t2 - t1, t3 - t2);
    return true;
}

int main(void)
{
    _stat_t keygen = { UINT32_MAX, 0, 0 };
    _stat_t sign = { UINT32_MAX, 0, 0 };
    _stat_t verify = { UINT32_MAX, 0, 0 };
    bool ok = true;

    printf("PQClean %s self-test, %u rounds\n", PQCLEAN_MLDSA_ALGNAME, MLDSA_TEST_ROUNDS);
    printf("pk %u B, sk %u B, sig %u B, msg %u B\n", (unsigned)sizeof(_pk), (unsigned)sizeof(_sk),
           (unsigned)sizeof(_sig), (unsigned)sizeof(_msg));

    ztimer_acquire(ZTIMER_USEC);
    for (unsigned i = 0; i < MLDSA_TEST_ROUNDS; i++) {
        if (!_round(i, &keygen, &sign, &verify)) {
            ok = false;
            break;
        }
    }
    ztimer_release(ZTIMER_USEC);

    if (!ok) {
        puts("[FAILURE]");
        return 1;
    }

    _stat_print("keygen", &keygen, MLDSA_TEST_ROUNDS);
    _stat_print("sign", &sign, MLDSA_TEST_ROUNDS);
    _stat_print("verify", &verify, MLDSA_TEST_ROUNDS);
    puts("[SUCCESS]");
    return 0;
}
