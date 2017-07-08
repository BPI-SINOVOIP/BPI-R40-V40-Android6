LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../LIBRARY/config.mk

LOCAL_CFLAGS += $(AW_OMX_EXT_CFLAGS)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= omx_adec.cpp

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../omxcore/inc/ \
	$(TOP)/frameworks/native/include/     \
	$(TOP)/frameworks/native/include/media/hardware \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/CODEC/AUDIO/DECODER/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/MEMORY/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/ \

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libbinder \
	libui       \
	libdl \
	libadecoder \
	libMemAdapter \
	

#libvdecoder
LOCAL_MODULE:= libOmxAdec

include $(BUILD_SHARED_LIBRARY)
