######################################set linux compile env #############################
ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk
OBJ_DIR=$(OBJ_BASE_DIR)/app
__LIB_DIR := $(LIB_DIR)

PROG = ctcsdk
SRCS = ctc_app_main.c

CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/core
CPPFLAGS += -I$(SDK_DIR)/libctccli/include
CPPFLAGS += -I$(SDK_DIR)/ctccli/sdkcli
CPPFLAGS += -I$(SDK_DIR)/app
CPPFLAGS += -I$(SDK_DIR)/app/config
CPPFLAGS += -I$(SDK_DIR)/app/usr
CPPFLAGS += -I$(SDK_DIR)/app/sample
CPPFLAGS += $(foreach n, $(CHIPNAME), -I$(SDK_DIR)/ctccli/sdkcli/$(n))
CPPFLAGS += -I$(SDK_DIR)/libptkxr/include

LIBSAL    = $(LIB_DIR)/libsal.a
LIBDAL    = $(LIB_DIR)/libdal.a
LIBSDK    = $(LIB_DIR)/libsdkcore.a
LIBDRV    = $(LIB_DIR)/libdrv.a
LIBAPP    = $(LIB_DIR)/libapp.a

LIBLIBCTCCLI  = $(LIB_DIR)/liblibctccli.a
LIBCTCCLI  = $(LIB_DIR)/libctccli.a

ifeq ($(TARGETBOARD),ctc-sim)
LIBMEMMODEL = $(LIB_DIR)/libmemmodel.a
LIBCTCLI  = $(LIB_DIR)/libcmodelcli.a
LIBCMODEL = $(LIB_DIR)/libcmodel.a
LIBMODELCPP = $(LIB_DIR)/libmodelcpp.so
endif

LIBDKIT = $(LIB_DIR)/libdkit.a

ifeq ($(SO_LIB),yes)
LIBCTCSDK = $(LIB_DIR)/libctcsdk.so
else
LIBCTCSDK = $(LIB_DIR)/libctcsdk.a
endif

LD_FLAGS += -Wl,--rpath=$(RPATH)

ifeq ($(ONE_LIB),yes)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -lctcsdk -lapp
else
DEP_LIBS = $(LIBAPP) $(LIBLIBCTCCLI)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -lapp -lctccli
ifeq ($(PUMP), TRUE)
DEP_LIBS += $(LIB_DIR)/libpump.a
LD_LIBS += -lpump
endif
endif



ifeq ($(TARGETBOARD),ctc-sim)
ifneq ($(CHIPNAME), $(filter $(CHIPNAME), greatbelt))
DEP_LIBS += $(LIBMODELCPP) $(LIBCMODEL)
LD_LIBS += -Wl,--start-group,-lmodelcpp,-lcmodel,-lcmodelcli,--end-group -ldl
else
DEP_LIBS += $(LIBCMODEL)
LD_LIBS += -Wl,--start-group,-lcmodel,-lcmodelcli,--end-group -ldl
endif
ifneq ($(USW),yes)
DEP_LIBS += $(LIBMEMMODEL)
LD_LIBS += -lmemmodel
endif
endif

ifeq ($(ONE_LIB), yes)
DEP_LIBS += $(LIBCTCSDK)
LD_LIBS += -Wl,--start-group,-lctcsdk,--end-group -lpthread -ldl
else
DEP_LIBS += $(LIBCTCCLI)  $(LIBSDK) $(LIBDRV) $(LIBDAL) $(LIBSAL) $(LIBDKIT)
LD_LIBS += -Wl,--start-group,-llibctccli,-lsdkcore,-ldrv,-lsal,-ldal,-ldkit,--end-group -lpthread -ldl
ifeq ($(ChipAgent),TRUE)
DEP_LIBS += $(LIB_DIR)/libchipagent.a
LD_LIBS += -lchipagent
endif
endif

ifeq ($(PUMP), TRUE)
LD_LIBS += -lcurl -lrt
endif

ifeq ($(USW),yes)
ifeq ($(TARGETBOARD),ctc-sim)
CPPFLAGS += -I/opt/openssl-1.0.2c/usr/local/include/openssl
ifeq ($(M64),TRUE)
LIB_LCRYPTO_DIR := /opt/openssl-1.0.2c/usr/local/lib64
LD_LIBS += -L$(LIB_LCRYPTO_DIR) -lcrypto -ldl -lstdc++
else
LIB_LCRYPTO_DIR := /opt/openssl-1.0.2c/usr/local/lib
LD_LIBS += -L$(LIB_LCRYPTO_DIR) -lcrypto -ldl -lstdc++
endif
endif
endif

include $(MK_DIR)/prog.mk
endif

######################################set vxworks compile env #############################
ifeq ($(targetbase),vxworks)

OBJ_DIR=$(OBJ_BASE_DIR)/app
ifeq ($(LINK),yes)
all_targets = sample config sdkinit ctcsdk vxworks
else
all_targets = sample config sdkinit ctcsdk
endif

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE


usr: FORCE
	make -C $(SDK_DIR)/app/usr -f Makefile.vx

clean_usr: FORCE
	make -C $(SDK_DIR)/app/usr -f Makefile.vx clean
	
sample: FORCE
	make -C $(SDK_DIR)/app/sample -f Makefile.vx

clean_sample: FORCE
	make -C $(SDK_DIR)/app/sample -f Makefile.vx clean

config: FORCE
	make -C $(SDK_DIR)/app/config -f Makefile.vx

config_sample: FORCE
	make -C $(SDK_DIR)/app/config -f Makefile.vx clean

sdkinit: FORCE
	make -f Makefile.vx

clean_sdkinit: FORCE
	make -f Makefile.vx clean
	
ctcsdk: FORCE
	make -f Makefile_sdk.vx

clean_ctcsdk: FORCE
	make -f Makefile_sdk.vx clean

vxworks: FORCE
	make -f Makefile_vx

clean_vxworks: FORCE
	make -f Makefile_vx clean
	
.PHONY: FORCE
FORCE:
endif
