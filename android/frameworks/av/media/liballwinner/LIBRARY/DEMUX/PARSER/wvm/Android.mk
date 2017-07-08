LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LIB_ROOT=$(LOCAL_PATH)/../..
include $(LIB_ROOT)/../config.mk
include $(LIB_ROOT)/PARSER/config.mk

LOCAL_SRC_FILES:= \
	Extractor.cpp \
	ExtractorWrapper.cpp \
	WVMDataSource.cpp

LOCAL_C_INCLUDES:= \
	$(LOCAL_PATH)/../../../ \
	$(LOCAL_PATH)/../../BASE/include \
    $(LOCAL_PATH)/../../PARSER/include \
    $(LOCAL_PATH)/../../STREAM/include \
    $(LOCAL_PATH)/../../../CODEC/VIDEO/DECODER/include \
    $(LOCAL_PATH)/../../../CODEC/AUDIO/DECODER/include \
    $(LOCAL_PATH)/../../../CODEC/SUBTITLE/DECODER/include \
    $(TOP)/frameworks/av/include \
    $(TOP)/frameworks/av/media/libstagefright
		
LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

#LOCAL_CFLAGS :=-Werror


ifeq ($(BOARD_WIDEVINE_OEMCRYPTO_LEVEL), 1)
LOCAL_CFLAGS +=-DSECUREOS_ENABLED
endif

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        libstagefright \
        libstagefright_foundation \
        libdrmframework \
        libdl \
        libutils \
        libMemAdapter
        
LOCAL_MODULE:=libaw_wvm

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_SHARED_LIBRARY)

