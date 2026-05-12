APPLICATION = noelv_test_net

BOARD ?= zedboard-noelv
RIOTBASE ?= $(CURDIR)/../..

DEVELHELP ?= 1
QUIET     ?= 1

# Cadence GEM (PS GEM0) Ethernet driver
USEMODULE += cadence_gem

# GNRC network stack
USEMODULE += gnrc_netif
USEMODULE += gnrc_netif_ethernet
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo

# Shell + network commands
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += shell_cmd_gnrc_netif
USEMODULE += shell_cmd_gnrc_icmpv6_echo
# Pretty-print received packets (optional, remove to save space)
USEMODULE += gnrc_pktdump

include $(RIOTBASE)/Makefile.include
