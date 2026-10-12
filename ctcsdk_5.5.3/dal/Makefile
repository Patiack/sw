ifeq ($(targetbase),linux)
all_targets = libdal
all_targets += kdal

OBJ_DIR=$(OBJ_BASE_DIR)/dal

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

libdal: FORCE
	$(CTC_MAKE) -f Makefile_libdal

clean_libdal: FORCE
	$(CTC_MAKE) -f Makefile_libdal clean

kdal: FORCE
ifeq ($(BOARD),s6810)
	$(CTC_MAKE) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(SDK_DIR)/dal/src/linux/kernel -f Makefile
else
	$(CTC_MAKE) -C $(SDK_DIR)/dal/src/linux/kernel -f Makefile
endif

clean_kdal: FORCE
ifeq ($(BOARD),s6810)
	$(CTC_MAKE) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(SDK_DIR)/dal/src/linux/kernel -f Makefile clean
else
	$(CTC_MAKE) -C $(SDK_DIR)/dal/src/linux/kernel -f Makefile
endif

.PHONY: FORCE
FORCE:

endif


######################################set vxworks compile env #############################
ifeq ($(targetbase),vxworks)

OBJ_DIR=$(OBJ_BASE_DIR)/dal


all_targets = src libdal

all: $(all_targets) FORCE
clean: $(addprefix clean_,$(all_targets)) FORCE

src: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dal/src -f Makefile.vx

clean_src: FORCE
	$(CTC_MAKE) -C $(SDK_DIR)/dal/src -f Makefile.vx clean

libdal: FORCE
	$(CTC_MAKE) -f Makefile_libdal

clean_libdal: FORCE
	$(CTC_MAKE) -f Makefile_libdal clean


.PHONY: FORCE
FORCE:
endif
