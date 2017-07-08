LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../LIBRARY/config.mk

LOCAL_CFLAGS += $(AW_OMX_EXT_CFLAGS)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= vdecoderAdapter.cpp \
                  vdecoderAdapterBase.cpp 


TARGET_GLOBAL_CFLAGS += -DTARGET_BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../omxcore/inc/ \
	$(TOP)/frameworks/native/include/     \
	$(TOP)/frameworks/native/include/media/hardware \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/CODEC/VIDEO/DECODER/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/MEMORY/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/VE/include \
	$(TOP)/frameworks/av/media/liballwinner/LIBRARY/ \
	$(TOP)/hardware/libhardware/include              \


LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libutils \
	libbinder \
	libui       \
	libdl \
	libVE \
	libvdecoder \
	libMemAdapter \
	libion        \
	libOmxVdec    
	

LOCAL_MODULE:= libOmxVdecAdapter

include $(BUILD_SHARED_LIBRARY)
