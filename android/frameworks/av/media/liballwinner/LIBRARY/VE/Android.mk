LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

include $(LOCAL_PATH)/../config.mk

################################################################################
## set flags for golobal compile and link setting.
################################################################################

CONFIG_FOR_COMPILE = 
CONFIG_FOR_LINK = 

LOCAL_CFLAGS += $(CONFIG_FOR_COMPILE)
LOCAL_MODULE_TAGS := optional

################################################################################
## set the source files
################################################################################
## set the source path to VPATH.
SourcePath = $(shell find $(LOCAL_PATH) -type d)
#SvnPath = $(shell find $(LOCAL_PATH) -type d | grep ".svn")
#SourcePath := $(filter-out $(SvnPath) $(BuildPath) $(ObjectPath) $(OutputPath) $(DependFilePath), $(SourcePath))


## set the source files.
tmpSourceFiles  = $(foreach dir,$(SourcePath),$(wildcard $(dir)/*.c))
SourceFiles  = $(foreach file,$(tmpSourceFiles),$(subst $(LOCAL_PATH)/,,$(file)))

## set the include path for compile flags.
LOCAL_SRC_FILES:= $(SourceFiles)
LOCAL_C_INCLUDES := \
				$(SourcePath) \
				$(LOCAL_PATH)/../ \
				$(LOCAL_PATH)/../CODEC/VIDEO/DECODER/include

LOCAL_SHARED_LIBRARIES := libcutils libutils

#libve
LOCAL_MODULE:= libVE

include $(BUILD_SHARED_LIBRARY)
