@defgroup pkg_pqclean  PQClean post-quantum cryptography
@ingroup  pkg
@brief    Portable C99 reference implementations from the PQClean project
@see      https://github.com/PQClean/PQClean

# Overview

[PQClean](https://github.com/PQClean/PQClean) collects clean, dependency-free
C99 implementations of post-quantum schemes. This package builds the "clean"
implementation of ML-DSA-44 (FIPS 204) together with the shared Keccak/SHAKE
code from `common/fips202.c`.

Modules:

- `pqclean_mldsa44`: ML-DSA-44 key generation, signing and verification
  (selected by default)
- `pqclean_fips202`: SHAKE128/SHAKE256/SHA3 (added automatically)
- `pqclean_randombytes`: `randombytes()` backed by @ref sys_random
  (added automatically)

# Usage

Add to the application Makefile:

```makefile
USEPKG += pqclean
```

Then either use the parameter-set independent wrapper

```c
#include "pqclean_mldsa.h"

uint8_t pk[PQCLEAN_MLDSA_PUBLICKEYBYTES];
uint8_t sk[PQCLEAN_MLDSA_SECRETKEYBYTES];
uint8_t sig[PQCLEAN_MLDSA_BYTES];
size_t siglen;

pqclean_mldsa_keypair(pk, sk);
pqclean_mldsa_sign(sig, &siglen, msg, msg_len, sk);
int res = pqclean_mldsa_verify(sig, siglen, msg, msg_len, pk);   /* 0 = valid */
```

or include the PQClean header of the scheme directly:

```c
#include "ml-dsa-44/clean/api.h"
```

# Notes

- Verification is deterministic and does not use `randombytes()`. Key
  generation and signing draw from the RIOT PRNG, whose seed quality on boards
  without a hardware RNG is suitable for demonstrations only.
- ML-DSA-44 keeps several polynomial vectors on the stack: verification needs
  about 36 KiB, signing about 52 KiB. Run these operations in a thread with a
  stack of at least 64 KiB.
- `fips202.c` allocates its SHAKE states with `malloc()` and frees them again,
  so the application needs a heap.
- `fips202.c` exports `sha3_256()`, `sha3_384()` and `sha3_512()`, which clash
  with @ref sys_hashes. The package renames its copies to `pqclean_sha3_*`.
