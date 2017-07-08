LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../LIBRARY/config.mk

LOCAL_CFLAGS += $(AW_OMX_EXT_CFLAGS)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= transform_color_format.c

ifeq ($(USE_NEW_DISPLAY),1)
    LOCAL_SRC_FILES += omx_vdec_newDisplay.cpp
else
    LOCAL_SRC_FILES += omx_vdec.cpp
endif

TARGET_GLOBAL_CFLAGS += -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../omxcore/inc/ \
	$(TOP)/frameworks/native/include/     \
	$(TOP)/frameworks/native/include/media/hardware \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/CODEC/VIDEO/DECODER/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/MEMORY/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/VE/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/ \
	$(TOP)/hardware/libhardware/include              \

ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_5_0))
ifeq ($(CONFIG_CHIP), $(OPTION_CHIP_1667))
LOCAL_C_INCLUDES  += $(TOP)/hardware/aw/hwc/astar/
endif
endif

ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
ifeq ($(CONFIG_CHIP), $(OPTION_CHIP_1667))
LOCAL_C_INCLUDES  += $(TOP)/hardware/aw/hwc/astar/
endif
endif

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libbinder \
	libui       \
	libdl \
	libVE \
	libvdecoder \
	libMemAdapter \
	libion        \
	

#libvdecoder
LOCAL_MODULE:= libOmxVdec

include $(BUILD_SHARED_LIBRARY)
