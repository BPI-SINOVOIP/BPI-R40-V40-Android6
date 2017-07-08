LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../LIBRARY/config.mk

ifeq ($(CMCC),no)
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
    awplayer.cpp          \
    demuxComponent.cpp    \
    awMessageQueue.cpp    \
    cache.cpp             \
    subtitleUtils.cpp     \
    awStreamingSource.cpp \
    awStreamListener.cpp  \
    AwHDCPModule.cpp

LOCAL_C_INCLUDES  := \
        $(TOP)/frameworks/av/                               \
        $(TOP)/frameworks/av/include/                       \
	$(TOP)/frameworks/av/media/libstagefright/include   \
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
        $(LOCAL_PATH)/../LIBRARY/MEMORY/include                    

# for subtitle character set transform.
ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_5_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else
LOCAL_C_INCLUDES += $(TOP)/external/icu4c/common
endif

ifeq ($(BOARD_USE_PLAYREADY), 1)
LOCAL_SRC_FILES += \
	awPlayReadyLicense.cpp
ifeq ($(PLAYREADY_DEBUG), 1)
PLAYREADY_DIR:= $(TOP)/vendor/playready
include $(PLAYREADY_DIR)/config.mk
LOCAL_CFLAGS += $(PLAYREADY_CFLAGS)
LOCAL_C_INCLUDES +=							\
	$(PLAYREADY_DIR)/source/inc                 \
	$(PLAYREADY_DIR)/source/oem/common/inc      \
	$(PLAYREADY_DIR)/source/oem/ansi/inc        \
	$(PLAYREADY_DIR)/source/results             \
	$(PLAYREADY_DIR)/source/tools/shared/common \
	$(PLAYREADY_DIR)/source/tools/shared/netio
else
include $(TOP)/hardware/aw/playready/config.mk
LOCAL_CFLAGS += $(PLAYREADY_CFLAGS)
LOCAL_C_INCLUDES +=							\
	$(TOP)/hardware/aw/playready/include/inc                 \
	$(TOP)/hardware/aw/playready/include/oem/common/inc      \
	$(TOP)/hardware/aw/playready/include/oem/ansi/inc        \
	$(TOP)/hardware/aw/playready/include/results             \
	$(TOP)/hardware/aw/playready/include/tools/shared/common \
	$(TOP)/hardware/aw/playready/include/tools/shared/netio
endif
LOCAL_SHARED_LIBRARIES += libplayreadypk
endif
       
LOCAL_MODULE_TAGS := optional
 
LOCAL_CFLAGS += -Werror

LOCAL_MODULE:= libawplayer

LOCAL_SHARED_LIBRARIES +=   \
        libutils            \
        libcutils           \
        libbinder           \
        libmedia            \
	libstagefright	    \
        libui               \
        libgui              \
        libplayer           \
        libaw_plugin        \
        libcdx_parser       \
        libcdx_stream       \
        libicuuc		\
	libMemAdapter
        

include $(BUILD_SHARED_LIBRARY)
endif
