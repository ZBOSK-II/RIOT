PKG_NAME    = pqclean
PKG_URL     = https://github.com/PQClean/PQClean
PKG_VERSION = 0586a824fc0d49df0b6b6e9179d8d15d06d0974f
PKG_LICENSE = CC0-1.0

# fetch only what is compiled: the shared Keccak/SHAKE code and the
# ML-DSA-44 "clean" reference implementation
PKG_SPARSE_PATHS += common
PKG_SPARSE_PATHS += crypto_sign/ml-dsa-44/clean

include $(RIOTBASE)/pkg/pkg.mk

# "fips202.h" and "randombytes.h" are included by bare name from common/
CFLAGS += -I$(PKG_SOURCE_DIR)/common

# fips202.c exports sha3_256/384/512, the same names as RIOT's hashes module.
# RIOT links every module object directly, so rename the PQClean copies.
CFLAGS += -Dsha3_256=pqclean_sha3_256
CFLAGS += -Dsha3_384=pqclean_sha3_384
CFLAGS += -Dsha3_512=pqclean_sha3_512

.PHONY: pqclean_mldsa44 pqclean_fips202

all: pqclean_mldsa44 pqclean_fips202

pqclean_mldsa44:
	$(QQ)"$(MAKE)" -C $(PKG_SOURCE_DIR)/crypto_sign/ml-dsa-44/clean \
	    -f $(RIOTBASE)/Makefile.base MODULE=$@

pqclean_fips202:
	$(QQ)"$(MAKE)" -C $(PKG_SOURCE_DIR)/common -f $(RIOTBASE)/Makefile.base \
	    MODULE=$@ SRC=fips202.c
