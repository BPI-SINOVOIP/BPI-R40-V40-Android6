###############################################################################
# WebView Chromium
LOCAL_PATH := $(call my-dir)

my_archs := arm arm64 x86 x86_64
my_src_arch := $(call get-prebuilt-src-arch, $(my_archs))

include $(CLEAR_VARS)
LOCAL_MODULE := WebViewGoogle
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_BUILT_MODULE_STEM := package.apk
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
#LOCAL_PRIVILEGED_MODULE :=
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_OVERRIDES_PACKAGES := webview
ifeq ($(my_src_arch),arm)
  LOCAL_SRC_FILES := $(LOCAL_MODULE).apk
else ifeq ($(my_src_arch),x86)
  LOCAL_SRC_FILES := $(LOCAL_MODULE)_$(my_src_arch).apk
else ifeq ($(my_src_arch),arm64)
  LOCAL_SRC_FILES := $(LOCAL_MODULE)_$(my_src_arch).apk
  LOCAL_MULTILIB := both
else ifeq ($(my_src_arch),x86_64)
  LOCAL_SRC_FILES := $(LOCAL_MODULE)_$(my_src_arch).apk
  LOCAL_MULTILIB := both
endif
LOCAL_REQUIRED_MODULES := \
    libwebviewchromium_loader \
    libwebviewchromium_plat_support
include $(BUILD_PREBUILT)
