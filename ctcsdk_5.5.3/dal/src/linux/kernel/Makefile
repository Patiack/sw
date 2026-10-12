
include $(MK_DIR)/sys_km.mk

OBJ_DIR=$(OBJ_BASE_DIR)/dal/src/linux/kernel

DAL_DIR = $(CURDIR)/../../..

LKMOBJDIR=$(OBJ_BASE_DIR)/dal/src/linux/kernel

obj-m := dal.o
dal-objs := dal_kernel.o dal_mpool.o
DAL_KO = dal.ko

EXTRA_CFLAGS += -I$(DAL_DIR)/include
EXTRA_CFLAGS += -I$(SDK_DIR)/sal/include
ifeq ($(BOARD),s6810)
EXTRA_CFLAGS +=	-DSOC_ACTIVE
endif

export DAL_DIR

.PHONY: all
all:
	cp $(DAL_DIR)/src/common/dal_mpool.c .
	cp $(DAL_DIR)/include/dal_*.h .
	mkdir -p $(LKMOBJDIR)
	$(CTC_MAKE) -C $(KDIR) M=$(shell pwd) LKMOBJDIR=$(LKMOBJDIR)
	if [ ! -d $(dir $(BLD_DIR)/lib/) ]; then mkdir -p $(dir $(BLD_DIR)/lib/); fi
	cp -f $(LKMOBJDIR)/$(DAL_KO) $(BLD_DIR)/lib
	rm dal_common.h dal_mpool.*


.PHONY: clean
clean:
	$(CTC_MAKE) -C $(KDIR) M=$(shell pwd) LKMOBJDIR=$(LKMOBJDIR) clean
	rm -f $(BLD_DIR)/lib/$(DAL_KO)
