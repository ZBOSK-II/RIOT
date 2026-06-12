APPLICATION = noelv_test_greth

BOARD ?= zedboard-noelv
RIOTBASE ?= $(CURDIR)/../..

DEVELHELP ?= 1
QUIET     ?= 1

# GRETH driver (Gaisler Ethernet MAC, PMOD-JD RMII PHY)
USEMODULE += greth

# GNRC network stack
USEMODULE += gnrc_netif
USEMODULE += gnrc_netif_ethernet
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo

# UDP support (sock_udp API via GNRC backend)
USEMODULE += gnrc_udp
USEMODULE += gnrc_sock_udp

# Shell + network commands
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += shell_cmd_gnrc_netif
USEMODULE += shell_cmd_gnrc_icmpv6_echo
USEMODULE += gnrc_pktdump

include $(RIOTBASE)/Makefile.include
