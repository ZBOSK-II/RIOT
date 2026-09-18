INCLUDES += -I$(APPDIR)

include ../Makefile.drivers_common

USEMODULE += test_utils_netdev_eth_minimal
USEMODULE += greth

BOARD ?= zedboard-noelv

# GRETH is a GRLIB on-SoC MAC: the driver uses RISC-V cache-management
# instructions and the PLIC, so it can only be built for the NOEL-V boards
BOARDS_SUPPORTED := arty-a7-noelv zedboard-noelv
CFLAGS += -DEVENT_THREAD_STACKSIZE_DEFAULT=1024

include $(RIOTBASE)/Makefile.include
