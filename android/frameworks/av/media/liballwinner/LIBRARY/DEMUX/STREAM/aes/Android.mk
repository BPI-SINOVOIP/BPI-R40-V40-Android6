LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LIB_ROOT=$(LOCAL_PATH)/../..
include $(LIB_ROOT)/../config.mk
include $(LIB_ROOT)/STREAM/config.mk

LOCAL_SRC_FILES = \
		$(notdir $(wildcard $(LOCAL_PATH)/*.c))

ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
	LOCAL_C_INCLUDES:= \
	    $(LIB_ROOT)/BASE/include \
	    $(LIB_ROOT)/STREAM/include \
	    $(TOP)/external/boringssl/src/include \
	    $(LIB_ROOT)/../           
else
	LOCAL_C_INCLUDES:= \
	    $(LIB_ROOT)/BASE/include \
	    $(LIB_ROOT)/STREAM/include \
	    $(TOP)/external/openssl/include \
	    $(LIB_ROOT)/../           
endif

LOCAL_CFLAGS += $(CDX_CFLAGS)

LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE:= libcdx_aes_stream

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)
