
export COMPATIBAL_CHIP

ifeq ($(findstring goldengate, $(CHIPNAME)),goldengate)
COMPATIBAL_CHIP += goldengate
endif

ifeq ($(findstring greatbelt, $(CHIPNAME)),greatbelt)
COMPATIBAL_CHIP += greatbelt
endif

ifeq ($(TARGETBOARD),vxworks-board)
CPPFLAGS += -DSDK_IN_VXWORKS
CPPFLAGS += -DSDK_WORK_ENV=0
CPPFLAGS += -DSDK_WORK_PLATFORM=0
endif

ifeq ($(TARGETBOARD),vxworks-sim)
CPPFLAGS += -DSDK_IN_VXWORKS
CPPFLAGS += -DUSE_SIM_MEM
CPPFLAGS += -DSDK_WORK_ENV=0
CPPFLAGS += -DSDK_WORK_PLATFORM=1
endif

ifneq ($(ARCH),powerpc)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(ARCH),x86)
CPPFLAGS += -DHOST_IS_LE=1
endif

ifeq ($(ARCH),mips)
ifeq ($(CPU),ls2f)
CPPFLAGS += -DHOST_IS_LE=1
else
CPPFLAGS += -DHOST_IS_LE=0
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

ifneq ($(VER),r)
CPPFLAGS += -DSDK_IN_DEBUG_VER
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

ifeq ($(findstring duet2, $(CHIPNAME)),duet2)
CPPFLAGS += -DDUET2
CPPFLAGS += -DUSW
endif

ifeq ($(findstring tsingma, $(CHIPNAME)),tsingma)
CPPFLAGS += -DTSINGMA
CPPFLAGS += -DUSW
endif

ifeq ($(findstring goldengate, $(CHIPNAME)),goldengate)
CPPFLAGS += -DGOLDENGATE
endif

ifeq ($(findstring humber, $(CHIPNAME)),humber)
CPPFLAGS += -DHUMBER
endif

ifeq ($(findstring greatbelt, $(CHIPNAME)),greatbelt)
CPPFLAGS += -DGREATBELT
endif

CPPFLAGS += -I$(VX_HEADER)/
CPPFLAGS += -I$(VX_HEADER)/wrn/coreip

ifeq ($(CPU),SIMNT)
CFLAGS += -fno-strict-aliasing
else
CFLAGS += -fno-strict-aliasing -mlongcall
endif

ifneq ($(VER),r)
CPPFLAGS += -DSDK_IN_DEBUG_VER
endif

CFLAGS += $(CTC_CFLAGS)
#add by zzhu for gcov
IS_GCOV = no
