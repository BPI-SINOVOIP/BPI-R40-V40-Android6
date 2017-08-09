###############################################################################
# FaceLock
LOCAL_PATH := $(call my-dir)

my_archs := arm arm64 x86 x86_64
my_src_arch := $(call get-prebuilt-src-arch, $(my_archs))

include $(CLEAR_VARS)
LOCAL_MODULE := FaceLock
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_BUILT_MODULE_STEM := package.apk
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
#LOCAL_PRIVILEGED_MODULE :=
LOCAL_CERTIFICATE := PRESIGNED
#LOCAL_OVERRIDES_PACKAGES :=
LOCAL_SRC_FILES := $(LOCAL_MODULE).apk
LOCAL_REQUIRED_MODULES := \
    detection/multi_pose_face_landmark_detectors.8/landmark_group_meta_data.bin \
    detection/multi_pose_face_landmark_detectors.8/left_eye-y0-yi45-p0-pi45-r0-ri20.lg_32-tree7-wmd.bin \
    detection/multi_pose_face_landmark_detectors.8/nose_base-y0-yi45-p0-pi45-r0-ri20.lg_32-tree7-wmd.bin \
    detection/multi_pose_face_landmark_detectors.8/right_eye-y0-yi45-p0-pi45-r0-ri20.lg_32-3-tree7-wmd.bin \
    detection/yaw_roll_face_detectors.7.1/head-y0-yi45-p0-pi45-r0-ri30.4a-v24-tree7-2-wmd.bin \
    detection/yaw_roll_face_detectors.7.1/head-y0-yi45-p0-pi45-rn30-ri30.5-v24-tree7-2-wmd.bin \
    detection/yaw_roll_face_detectors.7.1/head-y0-yi45-p0-pi45-rp30-ri30.5-v24-tree7-2-wmd.bin \
    detection/yaw_roll_face_detectors.7.1/pose-r.8.1.bin \
    detection/yaw_roll_face_detectors.7.1/pose-y-r.8.1.bin \
    recognition/face.face.y0-y0-71-N-tree_7-wmd.bin
LOCAL_PREBUILT_JNI_LIBS := \
    lib/$(my_src_arch)/libfacelock_jni.so
LOCAL_MODULE_TARGET_ARCH := $(my_src_arch)
include $(BUILD_PREBUILT)

include $(LOCAL_PATH)/models/Android.mk

