BUILD_NANDTRIM := true

ifeq ($(BUILD_NANDTRIM), true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	nand_trim.c

LOCAL_SHARED_LIBRARIES := \
	libcutils
	
LOCAL_MODULE := nand_trim
LOCAL_MODULE_TAGS := eng

include $(BUILD_EXECUTABLE)

endif