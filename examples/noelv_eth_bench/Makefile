APPLICATION = noelv_eth_bench

BOARD ?= zedboard-noelv
RIOTBASE ?= $(CURDIR)/../..

DEVELHELP ?= 0
QUIET     ?= 1

USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif

# GNRC network stack
USEMODULE += gnrc_netif
USEMODULE += gnrc_netif_ethernet
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo

# UDP via GNRC sock
USEMODULE += gnrc_udp
USEMODULE += gnrc_sock_udp

# ztimer for throughput measurement
USEMODULE += ztimer_usec

# Shell + network commands
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += shell_cmd_gnrc_netif
USEMODULE += shell_cmd_gnrc_icmpv6_echo

include $(RIOTBASE)/Makefile.include
