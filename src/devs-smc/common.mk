ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

# Override default project name (dir is devs-smc, devs.mk prepends devs-)
override PROJECT = smc

SRCS = if_smc.c if_smc_fdt.c mii_bitbang.c

INTERFACE_PREFIX="smc"

EXTRA_INCVPATH += $(PROJECT_ROOT)

SO_VERSION = 1
LDFLAGS += -shared -Wl,--allow-shlib-undefined

define PINFO
PINFO DESCRIPTION="SMSC LAN91C111 Ethernet driver for io-sock"
endef

include $(QNX_TARGET)/usr/include/devs/devs.mk
