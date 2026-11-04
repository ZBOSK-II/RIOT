# Builds the host-side key/signature tool from the same PQClean sources the
# firmware uses. RIOT checks them out into build/pkg/pqclean on the first
# firmware build; point PQCLEAN_DIR at another checkout if needed.
RIOTBASE ?= $(abspath $(CURDIR)/../../../../..)
PQCLEAN_DIR ?= $(RIOTBASE)/build/pkg/pqclean
MLDSA_DIR = $(PQCLEAN_DIR)/crypto_sign/ml-dsa-44/clean

ifeq (,$(wildcard $(MLDSA_DIR)/api.h))
  $(error PQClean not found in $(PQCLEAN_DIR). Build the firmware once or set PQCLEAN_DIR)
endif

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

SRCS = mldsa_tool.c
SRCS += $(wildcard $(MLDSA_DIR)/*.c)
SRCS += $(PQCLEAN_DIR)/common/fips202.c
SRCS += $(PQCLEAN_DIR)/common/randombytes.c

KEYDIR = ../demo-keys
INCDIR = ../include
TESTVEC_MSG = NOEL-V ML-DSA-44 self-test

.PHONY: all keys clean

all: mldsa_tool

mldsa_tool: $(SRCS)
	$(CC) $(CFLAGS) -I$(PQCLEAN_DIR)/common -I$(MLDSA_DIR) -o $@ $(SRCS)

# regenerate the demo key pair, the public key header and the self-test vector
keys: mldsa_tool
	mkdir -p $(KEYDIR) $(INCDIR)
	./mldsa_tool keygen $(KEYDIR)/pk.bin $(KEYDIR)/sk.bin $(INCDIR)/mldsa_pubkey.h
	./mldsa_tool export-testvec $(KEYDIR)/sk.bin "$(TESTVEC_MSG)" $(INCDIR)/mldsa_testvec.h

clean:
	rm -f mldsa_tool
