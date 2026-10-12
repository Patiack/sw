######################################set linux compile env #############################
######################################set linux compile env #############################
ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk
OBJ_DIR=$(OBJ_BASE_DIR)/ctc_shell
__LIB_DIR := $(LIB_DIR)

PROG = ctc_shell

SRCS = ctc_shell.c

LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -lpthread

include $(MK_DIR)/prog.mk
endif

############################# vxworks #####################################
ifeq ($(targetbase),vxworks)
include $(MK_DIR)/sys_vx.mk

__LIB_DIR := $(LIB_DIR)

PROG = ctc_shell

SRCS = ctc_shell.c

LD_LIBS = -L$(__LIB_DIR) -L$(LIB_DIR) -lpthread

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
