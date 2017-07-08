
LOCAL_PATH:= $(call my-dir)

include $(LOCAL_PATH)/../config.mk

ifeq ($(CONFIG_PRODUCT),$(OPTION_PRODUCT_TVBOX))

#include $(call all-subdir-makefiles)

include $(CLEAR_VARS)
include $(call all-makefiles-under,$(LOCAL_PATH))

endif
