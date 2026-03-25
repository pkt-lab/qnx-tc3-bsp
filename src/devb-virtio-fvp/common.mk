ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

INSTALLDIR=sbin

define PINFO
PINFO DESCRIPTION="VirtIO MMIO block device driver for TC3 FVP"
endef

SRCS = devb-virtio-fvp.c
EXTRA_INCVPATH += $(PROJECT_ROOT)

include $(MKFILES_ROOT)/qtargets.mk
