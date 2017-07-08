LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../LIBRARY/config.mk

ifeq ($(CMCC),yes)
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
    cmccplayer.cpp        \
    demuxComponent.cpp    \
    awMessageQueue.cpp    \
    cache.cpp             \
    subtitleUtils.cpp     \
    awStreamingSource.cpp \
    awStreamListener.cpp  \
    awLogRecorder.cpp     \
    AwHDCPModule.cpp \
		
LOCAL_C_INCLUDES  := \
        $(TOP)/frameworks/av/                               \
        $(TOP)/frameworks/av/include/                       \
        $(TOP)/frameworks/native/include/                   \
        $(LOCAL_PATH)/../LIBRARY/CODEC/VIDEO/DECODER/include       \
        $(LOCAL_PATH)/../LIBRARY/CODEC/AUDIO/DECODER/include       \
        $(LOCAL_PATH)/../LIBRARY/CODEC/SUBTITLE/DECODER/include    \
        $(LOCAL_PATH)/../LIBRARY/PLAYER/include                    \
        $(LOCAL_PATH)/../LIBRARY/PLUGIN/include/            \
        $(LOCAL_PATH)/../LIBRARY/DEMUX/PARSER/include/      \
        $(LOCAL_PATH)/../LIBRARY/DEMUX/STREAM/include/      \
        $(LOCAL_PATH)/../LIBRARY/DEMUX/BASE/include/        \
        $(LOCAL_PATH)/../LIBRARY/                           \

# for subtitle character set transform.
ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_5_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else
LOCAL_C_INCLUDES += $(TOP)/external/icu4c/common
endif

        
LOCAL_MODULE_TAGS := optional
 
LOCAL_CFLAGS += -Werror

LOCAL_MODULE:= libawplayer

LOCAL_SHARED_LIBRARIES +=   \
        libutils            \
        libcutils           \
        libbinder           \
        libmedia            \
        libui               \
        libgui              \
        libplayer           \
        libaw_plugin        \
        libcdx_parser       \
        libcdx_stream       \
        libicuuc
        

include $(BUILD_SHARED_LIBRARY)
endif
