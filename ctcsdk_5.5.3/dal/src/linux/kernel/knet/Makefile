
include $(MK_DIR)/sys_km.mk

DAL_DIR = $(CURDIR)/../../../..

LKMOBJDIR=$(OBJ_BASE_DIR)/dal/src/linux/kernel/knet

obj-m := knet.o
knet-objs := knet_kernel.o dal_mpool.o
KNET_KO = knet.ko

EXTRA_CFLAGS += -I$(DAL_DIR)/include
EXTRA_CFLAGS += -I$(SDK_DIR)/sal/include
EXTRA_CFLAGS += $(CTC_CFLAGS)
ifeq ($(BOARD),s6810)
EXTRA_CFLAGS += -DSOC_ACTIVE
endif

.PHONY: all
all:
	cp $(DAL_DIR)/src/common/dal_mpool.c .
	cp $(DAL_DIR)/include/dal_*.h .
	$(MAKE) -C $(KDIR) M=$(shell pwd) LKMOBJDIR=$(LKMOBJDIR)
	if [ ! -d $(dir $(BLD_DIR)/bin.$(TARGETBOARD)/) ]; then mkdir -p $(dir $(BLD_DIR)/bin.$(TARGETBOARD)/); fi
	cp $(LKMOBJDIR)/$(KNET_KO) $(BLD_DIR)/bin.$(TARGETBOARD)
	rm dal_common.h dal_mpool.*


.PHONY: clean
clean:
	$(MAKE) -C $(KDIR) M=$(shell pwd) LKMOBJDIR=$(LKMOBJDIR) clean
	rm -f $(BLD_DIR)/bin.$(TARGETBOARD)/$(KNET_KO)
