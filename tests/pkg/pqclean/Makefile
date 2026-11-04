include ../Makefile.pkg_common

USEPKG += pqclean
USEMODULE += ztimer_usec

# key generation and signing keep several polynomial vectors on the stack
CFLAGS += -DTHREAD_STACKSIZE_MAIN=\(96*1024\)

include $(RIOTBASE)/Makefile.include
