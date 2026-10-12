ifeq ($(targetbase),linux)
include $(MK_DIR)/sys.mk

LIB = sal
SRCS = $(shell find src/linux_um -name "*.c")
SRCS += $(shell find src/common -name "*.c")

CPPFLAGS += -I$(SDK_DIR)/sal/include

include $(MK_DIR)/lib.mk

endif

ifeq ($(targetbase),vxworks)

OBJ_DIR=$(OBJ_BASE_DIR)/sal

all_targets = src libsal

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

src: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/sal/src -f Makefile.vx

clean_src: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/sal/src -f Makefile.vx clean

libsal: FORCE
	$(CTC_MAKE) -f Makefile_libsal.vx

clean_libsal: FORCE
	$(CTC_MAKE) -f Makefile_libsal.vx clean

.PHONY: FORCE
FORCE:
endif
