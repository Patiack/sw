# CROSS_COMPILE should set appropriate compiler path to PATH.
# Suggestion: add these two lines to your ~/.bash_profile
#    PATH=$PATH:/sw/pub/xtools.ppc-linux/bin/:/opt/gcc-4.3/bin/
#    export PATH
# In this case, /sw/pub/xtools.ppc-linux/bin/    /opt/gcc-4.3/bin/  is our appropriate compiler path.

####################### Set default param ##############3###
ifndef SDK_DIR
include Makefile.in
export SDK_DIR := $(CURDIR)
else
include $(SDK_DIR)/Makefile.in
endif

ifndef auto
export auto = no
endif

ifndef cpp
export cpp = no
endif

#Pump make arguments
#For chip visibility, TRUE or FALSE
ifndef PUMP
export PUMP = FALSE
endif

ifneq ($(CHIPNAME), $(filter $(CHIPNAME), greatbelt goldengate))
export USW = yes
else
export USW = no
endif

export MK_DIR := $(SDK_DIR)/mk
############################# linux #####################################
ifeq ($(targetbase),linux)

ifeq ($(LINUX_LK),TRUE)
export SDK_DIR = $(M)
endif

# ctcsdk: cli+sdk+cmodel (software simulation)
ifeq ($(TARGETBOARD),ctc-sim)
export CMODEL_DIR = $(CURDIR)/../cmodel

all_targets = libsal libdal libdrv liblibctccli libdkit
ifeq ($(CHIPNAME),greatbelt)
all_targets += libmemmodel
endif
ifeq ($(CHIPNAME),goldengate)
all_targets += libmemmodel
endif
#all_targets = libsal libdal libdrv libmemmodel liblibctccli
all_targets += libcmodel libcmodelcli libsdkcore libctccli

ifeq ($(CHIPNAME),goldengate)
all_targets += libmodelcpp
endif
ifeq ($(USW),yes)
all_targets += libmodelcpp
endif

ifeq ($(ChipAgent),TRUE)
all_targets += libchipagent
endif

ifeq ($(ONE_LIB),yes)
all_targets = libctcsdk libcmodel libcmodelcli
ifneq ($(CHIPNAME), $(filter $(CHIPNAME), greatbelt))
all_targets += libmodelcpp
endif
ifneq ($(USW),yes)
all_targets += libmemmodel
endif
endif

all_targets += libapp
image_targets += $(all_targets) app

dkcli_targets = libsal libdal libdrv libmemmodel liblibctccli libdkit
dkcli_targets += libdkcli

endif

# ctcsdk: cli+sdk++asic (hardware platform board)
ifeq ($(TARGETBOARD),linux-board)

ifeq ($(ONE_LIB), yes)
all_targets = libctcsdk
else
all_targets = libsal libdal libdrv liblibctccli libdkit
ifeq ($(ChipAgent),TRUE)
all_targets += libchipagent
endif
all_targets += libsdkcore libctccli
endif
ifeq ($(PUMP), TRUE)
all_targets += libpump
endif
#all_targets += libapp
all_targets += ctc_shell

image_targets += $(all_targets) app

dkcli_targets = libsal libdal libdrv liblibctccli libdkit
dkcli_targets += libdkcli

endif

ifeq ($(LINUX_LK),TRUE)
ifneq ($(MAKECMDGOALS), ctc_shell)
include $(SDK_DIR)/mk/sys_km.mk
include $(SDK_DIR)/Makefile.kernel
else
include $(SDK_DIR)/Makefile.user
endif
else
include $(SDK_DIR)/Makefile.user
endif

endif

############################# vxworks #####################################3

ifeq ($(targetbase),vxworks)

ifeq ($(BUILD_SPEC),SIMNTgnu)
CPU              = SIMNT
TOOL             = gnu
TOOL_FAMILY      = gnu
DEFAULT_RULE     = sdk.out
endif


ifeq ($(BUILD_SPEC),PPC603gnu)
CPU              = PPC603
TOOL             = gnu
TOOL_FAMILY      = gnu
DEFAULT_RULE     = sdk.out
endif

#include $(TGT_DIR)/h/make/defs.project

## build-configuration info

ifeq ($(BUILD_SPEC),PPC603gnu)
AR               = arppc
AS               = ccppc
CC               = ccppc
CC_ARCH_SPEC     = -mcpu=603 -mstrict-align
CFLAGS           = -O0 -mcpu=603 -Wall -mstrict-align -mlongcall -fno-builtin -I. -I$(WIND_BASE)/target/h/ -DCPU=PPC603 -DTOOL_FAMILY=gnu -DTOOL=gnu -D_WRS_KERNEL -DVXWORKS -DBE_HOST=1
CFLAGS_AS        = -g -mcpu=603 -mstrict-align -ansi -fno-builtin -P -xassembler-with-cpp -I. -I$(WIND_BASE)/target/h/ -I$(WIND_BASE)/target/h/wrn/coreip -DCPU=PPC603 -DTOOL_FAMILY=gnu -DTOOL=gnu -D_WRS_KERNEL
CPP              = ccppc -E -P
LD               = ldppc
LDDEPS           =
LDFLAGS          = -X -N
LD_PARTIAL       = ccppc -r -nostdlib -Wl,-X
LD_PARTIAL_FLAGS = -X -r
NM               = nmppc
OPTION_DEFINE_MACRO = -D
OPTION_DEPEND    = -M -w
OPTION_GENERATE_DEPENDENCY_FILE = -MD
OPTION_INCLUDE_DIR = -I
OPTION_LANG_C    = -xc
OPTION_UNDEFINE_MACRO = -U
#POST_BUILD_RULE  = $(PRJ_DIR)\PPC603gnu\XelApp.bat
PRJ_LIBS         =
SIZE             = sizeppc
TOOL_FAMILY      = gnu
endif

ifeq ($(BUILD_SPEC),SIMNTgnu)
AR               = arsimpc
AS               = ccsimpc
RANLIB           = ranlibsimpc
CC               = ccsimpc
CC_ARCH_SPEC     = -mpentium
CFLAGS           = -g -mpentium -ansi -fno-builtin -fno-defer-pop -I. -I$(WIND_BASE)/target/h/  -DCPU=SIMNT -DTOOL_FAMILY=gnu  -DTOOL=gnu
CFLAGS_AS        = -g -mpentium -ansi -fno-builtin -fno-defer-pop -P -xassembler-with-cpp -I. -I$(WIND_BASE)/target/h/  -DCPU=SIMNT -DTOOL_FAMILY=gnu  -DTOOL=gnu
CPP              = ccsimpc -E -P
LD               = ldsimpc
LDFLAGS          = --subsystem=windows
LD_PARTIAL       = ccsimpc -r -nostdlib
LD_PARTIAL_FLAGS = -r
NM               = nmsimpc -g
OPTION_DEFINE_MACRO = -D
OPTION_DEPEND    = -M -w
OPTION_GENERATE_DEPENDENCY_FILE = -MD
OPTION_INCLUDE_DIR = -I
OPTION_LANG_C    = -xc
OPTION_UNDEFINE_MACRO = -U
SIZE             = sizesimpc
TOOL_FAMILY      = gnu
POST_BUILD_RULE  =
PRJ_LIBS         =
endif


# override make definitions only above this line
include $(TGT_DIR)/h/make/rules.project

# should change these two variable according to your compile environment
# LINK: YES - build ctcsdk vxWorks image , NO - build libctcsdk.a
export LINK = YES
# SPLIT_CORE: NO - libsdkcore.a , YES - libsdkcoreapi.a libsdkcoresys.a
export SPLIT_CORE = YES

all_targets = libsal libdal libdrv libsdkcore liblibctccli libctccli libdkit

ifeq ($(ChipAgent),TRUE)
all_targets += libchipagent
endif

# ctcsdk: sdk + asic
ifeq ($(TARGETBOARD),vxworks)
all_targets += libapp
image_targets += $(all_targets) app
dkcli_targets = libsal libdal libdrv liblibctccli libdkit
dkcli_targets += libdkcli
endif

# ctcsdk: sdk + memmodel
ifeq ($(TARGETBOARD),vxworks-sim)
ifneq ($(USW),yes)
all_targets += libmemmodel
endif
all_targets += libapp
image_targets += $(all_targets) app
dkcli_targets = libsal libdal libdrv libmemmodel liblibctccli libdkit
dkcli_targets += libdkcli

endif

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

dkcli: $(dkcli_targets) FORCE
clean_dkcli: $(addprefix clean_,$(dkcli_targets)) FORCE

libsal: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/sal

clean_libsal: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/sal clean

libdal: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dal -f Makefile

clean_libdal: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dal clean

libdrv: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver

clean_libdrv: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/driver clean

libsdkcore: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/core

clean_libsdkcore: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/core clean

libdkit: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dkits -f Makefile.lib

clean_libdkit: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dkits -f Makefile.lib clean

libdkcli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dkits -f Makefile

clean_libdkcli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dkits -f Makefile clean

libmemmodel: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/mem_model/$(CHIPNAME)

clean_libmemmodel: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/mem_model/$(CHIPNAME) clean

libchipagent: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/chip_agent

clean_libchipagent: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/chip_agent clean
liblibctccli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/libctccli

clean_liblibctccli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/libctccli clean

libctccli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/ctccli

clean_libctccli: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/ctccli clean

libappusr:	FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/app/usr

clean_libappusr:	FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/app/usr clean

libapp:	FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/app

clean_libapp:	FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/app clean

distclean: FORCE
	rm -rf $(BLD_DIR)

.PHONY: FORCE
FORCE:

endif
