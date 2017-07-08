LOCAL_PATH := $(call my-dir)



ifeq ($(BOARD_HAVE_BLUETOOTH_RTK),true)
# RealTek Bluetooth private configuration table
bdroid_CFLAGS := -Wno-unused-parameter
rtkbt_bdroid_C_INCLUDES += $(LOCAL_PATH)/bta/hh
rtkbt_bdroid_C_INCLUDES += $(LOCAL_PATH)/bta/dm
rtkbt_bdroid_CFLAGS += -DBLUETOOTH_RTK
rtkbt_bdroid_CFLAGS += -DBLUETOOTH_RTK_API


ifeq ($(BOARD_HAVE_BLUETOOTH_RTK_COEX),true)
rtkbt_bdroid_CFLAGS += -DBLUETOOTH_RTK_COEX
endif

# Setup bdroid local make variables for handling configuration
ifneq ($(BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR),)
  bdroid_C_INCLUDES := $(BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR) $(rtkbt_bdroid_C_INCLUDES)
  bdroid_CFLAGS += -DHAS_BDROID_BUILDCFG  $(rtkbt_bdroid_CFLAGS)
else
  bdroid_C_INCLUDES := $(rtkbt_bdroid_C_INCLUDES)
  bdroid_CFLAGS += -DHAS_NO_BDROID_BUILDCFG  $(rtkbt_bdroid_CFLAGS)
endif
else
# Setup bdroid local make variables for handling configuration
ifneq ($(BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR),)
  bdroid_C_INCLUDES := $(BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR)
  bdroid_CFLAGS += -DHAS_BDROID_BUILDCFG
else
  bdroid_C_INCLUDES :=
  bdroid_CFLAGS += -DHAS_NO_BDROID_BUILDCFG
endif
endif

ifneq ($(BOARD_BLUETOOTH_BDROID_HCILP_INCLUDED),)
  bdroid_CFLAGS += -DHCILP_INCLUDED=$(BOARD_BLUETOOTH_BDROID_HCILP_INCLUDED)
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
bdroid_CFLAGS += -DBLUEDROID_DEBUG
endif

bdroid_CFLAGS += \
  -Wall \
  -Wno-unused-parameter \
  -Wunused-but-set-variable \
  -UNDEBUG \
  -DLOG_NDEBUG=1

include $(call all-subdir-makefiles)

# Cleanup our locals
bdroid_C_INCLUDES :=
bdroid_CFLAGS :=
