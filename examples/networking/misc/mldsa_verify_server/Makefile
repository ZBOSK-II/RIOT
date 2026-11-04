# name of your application
APPLICATION = mldsa_verify_server

# If no BOARD is found in the environment, use this default:
BOARD ?= arty-a7-noelv

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../../../..

# Networking: default Ethernet device of the board, IPv6, ping and TCP
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo
USEMODULE += gnrc_tcp

# Shell with ifconfig/ping/ps, interface statistics, microsecond timer
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ps
USEMODULE += netstats_l2
USEMODULE += netstats_ipv6
USEMODULE += ztimer_usec

# ML-DSA-44 from PQClean
USEPKG += pqclean
USEMODULE += pqclean_mldsa44

# TCP port the server listens on and the largest accepted message
MLDSA_SERVER_PORT ?= 7777
MLDSA_SERVER_MAX_MSG_LEN ?= 1024
CFLAGS += -DMLDSA_SERVER_PORT=$(MLDSA_SERVER_PORT)
CFLAGS += -DMLDSA_SERVER_MAX_MSG_LEN=$(MLDSA_SERVER_MAX_MSG_LEN)

# Ethernet-sized segments and a receive window that holds a whole request
# (12 B header + 1024 B message + 2420 B signature) in one go
CFLAGS += -DCONFIG_GNRC_TCP_MSS=1440
CFLAGS += -DCONFIG_GNRC_TCP_MSS_MULTIPLICATOR=4
CFLAGS += -DCONFIG_GNRC_PKTBUF_SIZE=16384

# The server has a single connection slot. Should it ever close first, do not
# keep that slot in TIME_WAIT for the default 60 s.
CFLAGS += -DCONFIG_GNRC_TCP_MSL_MS=1000

# `mldsa_selftest` verifies a signature from the shell thread, which needs
# about 36 KiB of stack for that
CFLAGS += -DTHREAD_STACKSIZE_MAIN=\(64*1024\)

# Set to 0 to leave the LEDs alone
MLDSA_SERVER_USE_LEDS ?= 1
CFLAGS += -DMLDSA_SERVER_USE_LEDS=$(MLDSA_SERVER_USE_LEDS)

# The GRETH in the prebuilt NOEL-ARTYA7 design (40 MHz AHB) cannot drain its
# RX FIFO at 100 Mbit/s: frames longer than about 260 bytes are dropped with
# an overrun. At 10 Mbit/s full-size frames arrive without loss.
ifeq (arty-a7-noelv,$(BOARD))
  CFLAGS += -DCONFIG_GRETH_ADVERTISE_10M_ONLY=1
endif

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
