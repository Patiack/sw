ifeq ($(targetbase),linux)

include $(MK_DIR)/sys.mk
OBJ_DIR=$(OBJ_BASE_DIR)/pump

LIB = pump
SRCS = $(wildcard *.c)
SRCS += $(wildcard app/*.c)
SRCS += $(wildcard nb_api/*.c)
SRCS += $(wildcard sb_api/*.c)

CPPFLAGS += -I$(SDK_DIR)/sal/include
CPPFLAGS += -I$(SDK_DIR)/core/common/include
CPPFLAGS += -I$(SDK_DIR)/core/api/include
CPPFLAGS += -I$(SDK_DIR)/core/
CPPFLAGS += -I$(SDK_DIR)/pump
CPPFLAGS += -I$(SDK_DIR)/pump/lib
CPPFLAGS += -I$(SDK_DIR)/pump/nb_api
CPPFLAGS += -I$(SDK_DIR)/pump/sb_api
CPPFLAGS += -I$(SDK_DIR)/pump/app

include $(MK_DIR)/lib.mk
endif
