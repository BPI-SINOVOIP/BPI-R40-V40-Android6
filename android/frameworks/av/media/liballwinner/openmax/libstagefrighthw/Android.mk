
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

include $(LOCAL_PATH)/../../LIBRARY/config.mk

LOCAL_SRC_FILES := \
    AwOMXPlugin.cpp                      \

LOCAL_CFLAGS += $(PV_CFLAGS_MINUS_VISIBILITY)
LOCAL_CFLAGS += $(AW_OMX_EXT_CFLAGS)
LOCAL_MODULE_TAGS := optional


LOCAL_C_INCLUDES :=    \
        $(TOP)/frameworks/av/media/liballwinner/LIBRARY/ \
        $(TOP)/frameworks/native/include/media/hardware \
        $(TOP)/frameworks/native/include/media/openmax \

LOCAL_SHARED_LIBRARIES :=       \
        libbinder               \
        libutils                \
        libcutils               \
        libdl                   \
        libui                   \

LOCAL_MODULE := libstagefrighthw

include $(BUILD_SHARED_LIBRARY)
