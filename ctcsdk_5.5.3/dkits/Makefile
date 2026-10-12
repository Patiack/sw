######################################set linux compile env #############################
######################################set linux compile env #############################
ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk
OBJ_DIR=$(OBJ_BASE_DIR)/dkits
__LIB_DIR := $(LIB_DIR)

PROG = dkcli

SRCS = ctc_dkit_main.c

CPPFLAGS += -I$(SDK_DIR)/dal/include
CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/driver
CPPFLAGS += -I$(SDK_DIR)/driver/api/
CPPFLAGS += -I$(SDK_DIR)/libctccli/include
CPPFLAGS += -I$(SDK_DIR)/dkits/common

ifeq ($(ARCH),um)
CPPFLAGS += $(foreach n, $(CHIPNAME), -I$(SDK_DIR)/mem_model/$(n)/include)
LIBMEMMODEL = $(LIB_DIR)/libmemmodel.a
endif

LIBSAL    = $(LIB_DIR)/libsal.a
LIBDAL    = $(LIB_DIR)/libdal.a
LIBDRV    = $(LIB_DIR)/libdrv.a
LIBLIBCTCCLI  = $(LIB_DIR)/liblibctccli.a

DKITCLI = $(LIB_DIR)/libdkit.a


ifeq ($(ARCH),um)
DEP_LIBS = $(LIBLIBCTCCLI) $(LIBDRV) $(LIBSAL) $(LIBDAL) $(LIBMEMMODEL) $(DKITCLI)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -ldkit -llibctccli -ldrv -ldal -lmemmodel -lsal -lpthread
else
DEP_LIBS = $(LIBLIBCTCCLI) $(LIBDRV) $(LIBSAL) $(LIBDAL) $(DKITCLI)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -ldkit -llibctccli -ldrv -ldal -lsal -lpthread
endif


include $(MK_DIR)/prog.mk
endif

############################# vxworks #####################################
ifeq ($(targetbase),vxworks)
include $(MK_DIR)/sys_vx.mk

__LIB_DIR := $(LIB_DIR)

PROG = dkcli

SRCS = ctc_dkit_main.c

CPPFLAGS += -I$(SDK_DIR)/dal/include
CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/driver
CPPFLAGS += -I$(SDK_DIR)/driver/api/
CPPFLAGS += -I$(SDK_DIR)/libctccli/include
CPPFLAGS += -I$(SDK_DIR)/dkits/common

ifeq ($(ARCH),um)
CPPFLAGS += $(foreach n, $(CHIPNAME), -I$(SDK_DIR)/mem_model/$(n)/include)
LIBMEMMODEL = $(LIB_DIR)/libmemmodel.a
endif

LIBSAL    = $(LIB_DIR)/libsal.a
LIBDAL    = $(LIB_DIR)/libdal.a
LIBDRV    = $(LIB_DIR)/libdrv.a
LIBLIBCTCCLI  = $(LIB_DIR)/liblibctccli.a

DKITCLI = $(LIB_DIR)/libdkit.a


ifeq ($(TARGETBOARD),vxworks-sim)
DEP_LIBS = $(LIBLIBCTCCLI) $(LIBDRV) $(LIBSAL) $(LIBDAL) $(LIBMEMMODEL) $(DKITCLI)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -ldkit -llibctccli -ldrv -ldal -lmemmodel -lsal
else
DEP_LIBS = $(LIBLIBCTCCLI) $(LIBDRV) $(LIBSAL) $(LIBDAL) $(DKITCLI)
LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -ldkit -llibctccli -ldrv -ldal -lsal
endif

OBJS = $(wildcard $(OBJ_DIR)/*.o) $(foreach n, $(CHIPNAME), $(wildcard $(OBJ_DIR)/$(n)/*.o))
DEPS = $(wildcard $(OBJ_DIR)/*.d) $(foreach n, $(CHIPNAME), $(wildcard $(OBJ_DIR)/$(n)/*.d))

$(BIN_DIR)/$(PROG): $(OBJS) $(DEP_LIBS)
	$(shell mkdir -p $(dir $@))
	@echo "LD  => $(subst $(BLD_DIR),build.$(CPU).$(VER),$@)" && \
	$(CC) $(LD_FLAGS) -o $@ $(OBJS) $(LD_LIBS)

.PHONY: clean
clean:
	rm -rf $(BIN_DIR)/$(PROG)

endif
