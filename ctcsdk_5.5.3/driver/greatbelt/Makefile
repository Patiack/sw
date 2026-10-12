ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk
LIB = drv
SRCS = $(shell find src -name "*.c")
CHIPNAME = greatbelt
CPPFLAGS += -I$(SDK_DIR)/dal/include
CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/sal/utility/include

ifeq ($(TARGETBOARD),ctc-sim)
CPPFLAGS += -I$(SDK_DIR)/../cmodel/$(CHIPNAME)/ctckal/include
CPPFLAGS += -I$(SDK_DIR)/../cmodel/$(CHIPNAME)/include
CPPFLAGS += -I$(SDK_DIR)/mem_model/$(CHIPNAME)/include
endif

ifeq ($(TARGETBOARD),linux-sim)
CPPFLAGS += -I$(SDK_DIR)/mem_model/$(CHIPNAME)/include
endif

CPPFLAGS += -I$(SDK_DIR)/driver

include $(MK_DIR)/lib.mk

CFLAGS += -Werror -Wall
endif

ifeq ($(targetbase),vxworks)
BLD_DIR=$(SDK_DIR)/build.$(CPU).$(VER)
OBJ_DIR=$(BLD_DIR)/obj.$(TARGETBOARD)/driver
LIB_DIR=$(BLD_DIR)/lib.$(TARGETBOARD)
BIN_DIR=$(BLD_DIR)/bin.$(TARGETBOARD)


all_targets = greatbelt libdrv

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

greatbelt: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver/greatbelt -f Makefile.vx

clean_greatbelt: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver/greatbelt -f Makefile.vx  clean

libdrv: FORCE
	$(CTC_MAKE) -f Makefile_libdrv.vx

clean_libdrv: FORCE
	$(CTC_MAKE) -f Makefile_libdrv.vx clean


.PHONY: FORCE
FORCE:
endif

