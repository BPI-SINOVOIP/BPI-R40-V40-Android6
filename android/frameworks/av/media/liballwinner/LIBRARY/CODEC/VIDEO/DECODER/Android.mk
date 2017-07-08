LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

include $(LOCAL_PATH)/../../../config.mk

current_path := $(LOCAL_PATH)

libvdecoder_src_common   :=  adapter.c
libvdecoder_src_common   +=  fbm.c
libvdecoder_src_common   +=  pixelFormat.c
libvdecoder_src_common   +=  sbm.c
libvdecoder_src_common   +=  vdecoder.c
libvdecoder_src_common   +=  videoengine.c

libvdecoder_inc_common := 	$(current_path) \
                    		$(LOCAL_PATH)/../../../VE/include \
		                    $(LOCAL_PATH)/../../../MEMORY/include \
		                    $(LOCAL_PATH)/../../../ \
		                    $(LOCAL_PATH)/include \
		                    $(LOCAL_PATH) \

LOCAL_SRC_FILES := $(libvdecoder_src_common)
LOCAL_C_INCLUDES := $(libvdecoder_inc_common)
LOCAL_CFLAGS := 
LOCAL_LDFLAGS := 

ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_5_0))
#LOCAL_LDFLAGS_arm := -Wl,--no-warn-shared-textrel,-Bsymbolic
else ifeq ($(CONFIG_OS_VERSION), $(OPTION_OS_VERSION_ANDROID_6_0))
#LOCAL_LDFLAGS_arm := -Wl,--no-warn-shared-textrel,-Bsymbolic
else
#LOCAL_LDFLAGS := -Wl,--no-warn-shared-textrel,-Bsymbolic
endif

LOCAL_MODULE_TAGS := optional

## add libaw* for eng/user rebuild
LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libui       \
	libdl       \
	libVE       \
	libMemAdapter
	

LOCAL_MODULE := libvdecoder

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))


