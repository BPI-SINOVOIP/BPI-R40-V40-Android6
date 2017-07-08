LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := sdcard.c
LOCAL_MODULE := sdcard
LOCAL_CFLAGS := -Wall -Wno-unused-parameter -Werror
ifeq ($(strip $(BOARD_FUSE_SDCARD)),true)
LOCAL_CFLAGS += -DFUSE_SDCARD
endif
LOCAL_SHARED_LIBRARIES := libcutils

include $(BUILD_EXECUTABLE)
