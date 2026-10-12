ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk
LIB = drv
SRCS =
#SRCS += $(shell find ../common/src -name "*.c")
SRCS += $(shell find src -name "*.c")

CPPFLAGS += -I$(SDK_DIR)/dal/include
CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/sal/utility/include
CPPFLAGS += -I$(ROOT_DIR)/libctccli/include
CPPFLAGS += -I$(ROOT_DIR)/afx/

ifeq ($(TARGETBOARD),ctc-sim)
CPPFLAGS += -I$(SDK_DIR)/../cmodel/ctckal/include
CPPFLAGS += -I$(SDK_DIR)/../cmodel/goldengate/include
CPPFLAGS += -I$(SDK_DIR)/mem_model/goldengate/include
endif

ifeq ($(TARGETBOARD),linux-sim)
CPPFLAGS += -I$(SDK_DIR)/mem_model/goldengate/include
endif

ifeq ($(TARGETBOARD),ctc-sim)
CPPFLAGS += -DDRVIER_WORK_PLATFORM=1
else
CPPFLAGS += -DDRVIER_WORK_PLATFORM=0
endif

ifneq ($(ARCH),ppc)
#CPPFLAGS += -DCTC_HOST_IS_LITTLE_ENDIAN=1
#CPPFLAGS += -DHOST_IS_LE=1
else
#CPPFLAGS += -DCTC_HOST_IS_LITTLE_ENDIAN=0
#CPPFLAGS += -DHOST_IS_LE=0
endif

#CPPFLAGS += -I$(SDK_DIR)/driver/common/include
CPPFLAGS += -I$(SDK_DIR)/driver/goldengate/include

include $(MK_DIR)/lib.mk

CFLAGS += -Werror -Wall
endif

ifeq ($(targetbase),vxworks)
BLD_DIR=$(SDK_DIR)/build.$(CPU).$(VER)
OBJ_DIR=$(BLD_DIR)/obj.$(TARGETBOARD)/driver
LIB_DIR=$(BLD_DIR)/lib.$(TARGETBOARD)
BIN_DIR=$(BLD_DIR)/bin.$(TARGETBOARD)
 

#all_targets = common goldengate libdrv 
all_targets = goldengate libdrv

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

#common: FORCE
#	$(CTC_MAKE) -C $(SDK_DIR)/driver/common -f Makefile.vx
#
#clean_common: FORCE
#	$(CTC_MAKE) -C $(SDK_DIR)/driver/common -f Makefile.vx  clean	
#	
goldengate: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver/goldengate -f Makefile.vx

clean_goldengate: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver/goldengate -f Makefile.vx  clean		
	
libdrv: FORCE
	$(CTC_MAKE) -f Makefile_libdrv.vx

clean_libdrv: FORCE
	$(CTC_MAKE) -f Makefile_libdrv.vx clean	
		

.PHONY: FORCE
FORCE:
endif

