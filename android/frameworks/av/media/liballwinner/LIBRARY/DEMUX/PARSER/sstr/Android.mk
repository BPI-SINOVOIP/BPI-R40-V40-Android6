LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LIB_ROOT=$(LOCAL_PATH)/../..
include $(LIB_ROOT)/../config.mk
include $(LIB_ROOT)/PARSER/config.mk

LOCAL_SRC_FILES = \
		$(notdir $(wildcard $(LOCAL_PATH)/*.c)) \

LOCAL_C_INCLUDES:= \
    $(LOCAL_PATH)/../sstr/ \
	$(LIB_ROOT)/BASE/include \
    $(LIB_ROOT)/STREAM/include \
    $(LIB_ROOT)/PARSER/include \
    $(TOP)/external/libxml2/include \
    $(LIB_ROOT)/../CODEC/VIDEO/DECODER/include    \
    $(LIB_ROOT)/../CODEC/AUDIO/DECODER/include    \
    $(LIB_ROOT)/../CODEC/SUBTITLE/DECODER/include \
    $(LIB_ROOT)/../PLAYER/                 \
    $(LIB_ROOT)/../     \
    #$(TOP)/prebuilts/gcc/linux-x86/host/i686-linux-glibc2.7-4.6/sysroot/usr/include \
    #$(LOCAL_PATH)/../sstr/libiconv-1.10/include \

ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_5_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
LOCAL_C_INCLUDES += $(TOP)/external/icu/icu4c/source/common
else
LOCAL_C_INCLUDES += $(TOP)/external/icu4c/common
endif

ifeq ($(CONFIG_PRODUCT), $(OPTION_PRODUCT_TVBOX))
	ifeq ($(CONFIG_CHIP), $(OPTION_CHIP_1689))
	#ifeq ($(BOARD_USE_PLAYREADY), 1)
		#ifeq ($(PLAYREADY_DEBUG), 1)
			#include $(TOP)/vendor/playready/config.mk
			#LOCAL_CFLAGS += $(PLAYREADY_CFLAGS)
		#else
			#include $(TOP)/hardware/aw/playready/config.mk
			#LOCAL_CFLAGS += $(PLAYREADY_CFLAGS)
		#endif
	#endif
	LOCAL_CFLAGS += -DBOARD_PLAYREADY_USE_SECUREOS=${BOARD_PLAYREADY_USE_SECUREOS}
	endif
endif

LOCAL_STATIC_LIBRARIES = libxml2
LOCAL_CFLAGS += $(CDX_CFLAGS)

LOCAL_MODULE_TAGS := optional
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE := libcdx_sstr_parser

ifeq ($(TARGET_ARCH),arm)
    LOCAL_CFLAGS += -Wno-psabi
endif

include $(BUILD_STATIC_LIBRARY)

