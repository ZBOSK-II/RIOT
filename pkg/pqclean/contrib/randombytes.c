/*
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     pkg_pqclean
 * @{
 *
 * @file
 * @brief       PQClean randombytes() backed by the RIOT PRNG
 *
 * Only key generation and signing draw randomness, verification does not.
 * The result is as good as the seed of @ref sys_random, which on boards
 * without a hardware RNG makes it suitable for demonstrations only.
 *
 * @author      Matvii Ivashchenko
 *
 * @}
 */

#include <stddef.h>
#include <stdint.h>

#include "random.h"

/* PQClean's common/randombytes.h declares randombytes() and renames it to
 * PQCLEAN_randombytes with a macro, so this is the symbol the schemes call */
int PQCLEAN_randombytes(uint8_t *output, size_t n);

int PQCLEAN_randombytes(uint8_t *output, size_t n)
{
    random_bytes(output, n);
    return 0;
}
