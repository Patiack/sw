override SDK_DIR := $(shell cd $(SDK_DIR) && pwd)

#### Modified by xgu, for gbsystem, 20121122, Start
override TOP_DIR := $(shell cd $(TOP_DIR) && pwd)

OBJ_DIR = $(BLD_DIR)/obj/$(subst $(TOP_DIR)/,,$(CURDIR))
LIB_DIR = $(BLD_DIR)/lib
BIN_DIR = $(BLD_DIR)/bin
#### Modified by xgu, for gbsystem, 20121122, End

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar cru
RANLIB = $(CROSS_COMPILE)ranlib

export COMPATIBAL_CHIP

ifeq ($(findstring goldengate, $(CHIPNAME)),goldengate)
COMPATIBAL_CHIP += goldengate
endif

ifeq ($(findstring greatbelt, $(CHIPNAME)),greatbelt)
COMPATIBAL_CHIP += greatbelt
endif

ifeq ($(ARCH),powerpc)
CPPFLAGS += -DHOST_IS_LE=0
endif

ifeq ($(ARCH),x86)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(ARCH),mips)
ifeq ($(CPU),ls2f)
CPPFLAGS += -DHOST_IS_LE=1
else ifeq ($(CPU),ls2k)
CPPFLAGS += -DHOST_IS_LE=1
else
CPPFLAGS += -DHOST_IS_LE=0
CPPFLAGS += -mabi=n32
endif
endif

ifeq ($(ARCH),freescale)
CPPFLAGS += -DHOST_IS_LE=0
endif

ifeq ($(ARCH),arm)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(ARCH),arm64)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(ARCH),um)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(CPU),ls2f)
ifneq ($(M64),TRUE)
CPPFLAGS += -DDAL_USE_MMAP2
endif
endif

#################################
#SDK_WORK_ENV
#0 - with no cmodel;
#1 - with  cmodel;
#################################

#################################
#SDK_WORK_PLATFORM
#0 - hardware platform ;
#1 - software simulation platform;
#################################

ifeq ($(TARGETBOARD),ctc-sim)
CPPFLAGS += -DSDK_IN_USERMODE
CPPFLAGS += -DUSE_SIM_MEM
CPPFLAGS += -DSDK_WORK_ENV=1
CPPFLAGS += -DSDK_WORK_PLATFORM=1
ifeq ($(VER),d)
RPATH += $(LIB_DIR)
else
ifeq ($(M64),TRUE)
RPATH += /data01/users/sdk/humber_release/share/dll_x64/$(CHIPNAME)
else
RPATH += /data01/users/sdk/humber_release/share/dll/$(CHIPNAME)
endif
endif
endif

ifeq ($(TARGETBOARD),linux-board)
CPPFLAGS += -DSDK_IN_USERMODE
CPPFLAGS += -DSDK_WORK_PLATFORM=0

ifeq ($(ChipAgent),TRUE)
CPPFLAGS += -DSDK_WORK_ENV=1
else
CPPFLAGS += -DSDK_WORK_ENV=0
endif

endif

ifeq ($(M64),TRUE)
ifeq ($(ARCH),freescale)
CPPFLAGS += -fPIC -Wno-long-long -Wformat
endif
ifeq ($(ARCH), arm64)
CPPFLAGS += -fPIC -Wno-long-long -Wformat
else
LD_FLAGS += -m64
CPPFLAGS += -fPIC -Wno-long-long -Wformat -m64
endif
else
ifeq ($(CPU),ls2f)
LD_FLAGS += -mabi=n32
else ifeq ($(CPU),ls2k)
#LD_FLAGS += -mabi=n32
else
ifneq ($(ARCH),arm)
CPPFLAGS += -m32
LD_FLAGS += -m32
endif
endif
endif

ifneq ($(VER),r)
CPPFLAGS += -DSDK_IN_DEBUG_VER
endif

CFLAGS += -fno-strict-aliasing

IS_GCOV = no

ifeq ($(IS_GCOV),yes)
CPPFLAGS += -DISGCOV
endif

ifeq ($(DRV_DS_LITE),yes)
CPPFLAGS += -DDRV_DS_LITE
endif

ifeq ($(findstring l2, $(featureMode)),l2)
CPPFLAGS += -DFEATURE_MODE=1
else
	ifeq ($(findstring l2l3, $(featureMode)),l2l3)
	CPPFLAGS += -DFEATURE_MODE=2
	else
	CPPFLAGS += -DFEATURE_MODE=0
	endif
endif

ifeq ($(findstring greatbelt, $(CHIPNAME)),greatbelt)
CPPFLAGS += -DGREATBELT
ifeq ($(ChipAgent),TRUE)
CPPFLAGS += -DCHIP_AGENT
endif
endif

ifeq ($(findstring goldengate, $(CHIPNAME)),goldengate)
CPPFLAGS += -DGOLDENGATE
ifeq ($(ChipAgent),TRUE)
CPPFLAGS += -DCHIP_AGENT
endif
endif

ifeq ($(findstring duet2, $(CHIPNAME)),duet2)
CPPFLAGS += -DDUET2
CPPFLAGS += -DUSW
endif

ifeq ($(findstring tsingma, $(CHIPNAME)),tsingma)
CPPFLAGS += -DTSINGMA
CPPFLAGS += -DUSW
endif
CFLAGS += $(CTC_CFLAGS)
#CFLAGS += -Wframe-larger-than=1024

ifeq ($(PUMP), TRUE)
CPPFLAGS += -DCTC_PUMP
endif
