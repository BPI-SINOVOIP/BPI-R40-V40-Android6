include device/softwinner/common/BoardConfigCommon.mk

# Primary Arch
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := false
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a7

TARGET_BOARD_PLATFORM := azalea
TARGET_USE_NEON_OPTIMIZATION := true

TARGET_CPU_SMP := true

TARGET_NO_BOOTLOADER := true
# opt di for sunxi platform
TARGET_USE_BOOSTUP_OPZ := true
TARGET_BOARD_PLATFORM := azalea
TARGET_BOOTLOADER_BOARD_NAME := exdroid
TARGET_BOOTLOADER_NAME := exdroid

START_STOP_RENDER := 1
ifeq ($(START_STOP_RENDER),1)
COMMON_GLOBAL_CFLAGS += -DSTART_STOP_RENDER
COMMON_GLOBAL_CPPFLAGS += -DSTART_STOP_RENDER
endif

WATERMARK := 1
ifeq ($(WATERMARK),1)
COMMON_GLOBAL_CFLAGS += -DWATERMARK
COMMON_GLOBAL_CPPFLAGS += -DWATERMARK
endif
BOARD_EGL_CFG := device/softwinner/azalea-common/egl/egl.cfg
#distinguish CHIP PLATFORM
SW_CHIP_PLATFORM := R40
BOARD_KERNEL_BASE := 0x40000000
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x8000

#SurfaceFlinger's configs
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true

# hardware module include file path
TARGET_HARDWARE_INCLUDE := \
    device/softwinner/azalea-common/hardware/include
BOARD_CHARGER_ENABLE_SUSPEND := true
SW_PLATFORM_KERNEL_VERSION := v3_10
TARGET_USES_ION := true

BOARD_SEPOLICY_DIRS := \
    device/softwinner/azalea-common/sepolicy

USE_OPENGL_RENDERER := true


