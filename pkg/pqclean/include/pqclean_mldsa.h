/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     pkg_pqclean
 * @{
 *
 * @file
 * @brief       Parameter-set independent wrapper for the PQClean ML-DSA API
 *
 * The parameter set follows the module that is compiled in
 * (`pqclean_mldsa44`, ...). Applications that need one specific set can
 * include its PQClean header directly, e.g. `ml-dsa-44/clean/api.h`.
 *
 * @author      Matvii Ivashchenko
 */

#include <stddef.h>
#include <stdint.h>

#include "modules.h"

#ifdef __cplusplus
extern "C" {
#endif

#if IS_USED(MODULE_PQCLEAN_MLDSA44) || defined(DOXYGEN)
#  include "ml-dsa-44/clean/api.h"

/**
 * @name    Sizes and entry points of the selected parameter set
 * @{
 */
/** public key size in bytes */
#  define PQCLEAN_MLDSA_PUBLICKEYBYTES PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES
/** secret key size in bytes */
#  define PQCLEAN_MLDSA_SECRETKEYBYTES PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES
/** signature size in bytes */
#  define PQCLEAN_MLDSA_BYTES          PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES
/** algorithm name, "ML-DSA-44" */
#  define PQCLEAN_MLDSA_ALGNAME        PQCLEAN_MLDSA44_CLEAN_CRYPTO_ALGNAME
/** key pair generation function */
#  define PQCLEAN_MLDSA_KEYPAIR        PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair
/** detached signature function */
#  define PQCLEAN_MLDSA_SIGNATURE      PQCLEAN_MLDSA44_CLEAN_crypto_sign_signature
/** detached signature verification function */
#  define PQCLEAN_MLDSA_VERIFY         PQCLEAN_MLDSA44_CLEAN_crypto_sign_verify
/** @} */
#else
#  error "pqclean: no ML-DSA parameter set module selected"
#endif

/**
 * @brief   Generate a key pair
 *
 * @param[out] pk   public key, @ref PQCLEAN_MLDSA_PUBLICKEYBYTES bytes
 * @param[out] sk   secret key, @ref PQCLEAN_MLDSA_SECRETKEYBYTES bytes
 *
 * @return  0 on success
 */
static inline int pqclean_mldsa_keypair(uint8_t *pk, uint8_t *sk)
{
    return PQCLEAN_MLDSA_KEYPAIR(pk, sk);
}

/**
 * @brief   Sign a message (pure ML-DSA, empty context string)
 *
 * @param[out] sig      signature, @ref PQCLEAN_MLDSA_BYTES bytes
 * @param[out] siglen   number of bytes written to @p sig
 * @param[in]  m        message
 * @param[in]  mlen     length of @p m
 * @param[in]  sk       secret key
 *
 * @return  0 on success
 */
static inline int pqclean_mldsa_sign(uint8_t *sig, size_t *siglen, const uint8_t *m, size_t mlen,
                                     const uint8_t *sk)
{
    return PQCLEAN_MLDSA_SIGNATURE(sig, siglen, m, mlen, sk);
}

/**
 * @brief   Verify a signature (pure ML-DSA, empty context string)
 *
 * @param[in] sig       signature
 * @param[in] siglen    length of @p sig
 * @param[in] m         message
 * @param[in] mlen      length of @p m
 * @param[in] pk        public key
 *
 * @return  0 if the signature is valid
 * @return  -1 otherwise
 */
static inline int pqclean_mldsa_verify(const uint8_t *sig, size_t siglen, const uint8_t *m,
                                       size_t mlen, const uint8_t *pk)
{
    return PQCLEAN_MLDSA_VERIFY(sig, siglen, m, mlen, pk);
}

#ifdef __cplusplus
}
#endif

/** @} */
