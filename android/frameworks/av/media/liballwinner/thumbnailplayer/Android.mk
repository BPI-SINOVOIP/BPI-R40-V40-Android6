LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

include $(LOCAL_PATH)/../LIBRARY/config.mk

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
    	avtimer.cpp \
    	tplayer.cpp
		
LOCAL_C_INCLUDES  := \
        $(TOP)/frameworks/av/                               \
        $(TOP)/frameworks/av/include/                       \
        $(LOCAL_PATH)/../LIBRARY/CODEC/VIDEO/DECODER/include       \
        $(LOCAL_PATH)/../LIBRARY/DEMUX/BASE/include/        \
        $(LOCAL_PATH)/../LIBRARY/MEMORY/include                    \
        $(LOCAL_PATH)/../LIBRARY/                           \
        
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -Werror -Wno-deprecated-declarations

LOCAL_MODULE:= libthumbnailplayer

LOCAL_SHARED_LIBRARIES +=   \
        libutils            \
        libcutils           \
        libbinder           \
        libmedia            \
        libui               \
        libgui              \
        libMemAdapter       \
        libvdecoder

include $(BUILD_SHARED_LIBRARY)

