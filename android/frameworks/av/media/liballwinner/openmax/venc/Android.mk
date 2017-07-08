LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../LIBRARY/config.mk

LOCAL_CFLAGS += $(AW_OMX_EXT_CFLAGS)
LOCAL_CFLAGS += -D__OS_ANDROID
TARGET_GLOBAL_CFLAGS += -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

ifeq ($(CONFIG_CHIP), $(OPTION_CHIP_1689))
LOCAL_SRC_FILES:= omx_venc.cpp omx_tsem.c
else
LOCAL_SRC_FILES:= neon_rgb2yuv.s omx_venc.cpp omx_tsem.c
endif

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../omxcore/inc/ \
	$(TOP)/frameworks/native/include/     \
	$(TOP)/frameworks/native/include/media/hardware \
	$(TOP)/frameworks/native/include/media/openmax \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/CODEC/VIDEO/ENCODER/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/MEMORY/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/VE/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/ \

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
	libui \
	libion \
	libVE \
	libMemAdapter \
	libvencoder
				

LOCAL_MODULE:= libOmxVenc

include $(BUILD_SHARED_LIBRARY)
