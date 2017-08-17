$(call inherit-product, build/target/product/full_base.mk)
$(call inherit-product, device/softwinner/azalea-common/azalea-common.mk)
$(call inherit-product-if-exists, device/softwinner/bpi-m2u-m2b-lcd5/modules/modules.mk)

DEVICE_PACKAGE_OVERLAYS := device/softwinner/bpi-m2u-m2b-lcd5/overlay \
                           $(DEVICE_PACKAGE_OVERLAYS)

PRODUCT_PACKAGES += Launcher3
PRODUCT_PACKAGES += \
    ESFileExplorer \
    VideoPlayer \
    Bluetooth 

#   PartnerChromeCustomizationsProvider

PRODUCT_COPY_FILES += \
    device/softwinner/bpi-m2u-m2b-lcd5/kernel:kernel \
    device/softwinner/bpi-m2u-m2b-lcd5/fstab.sun8iw11p1:root/fstab.sun8iw11p1 \
    device/softwinner/bpi-m2u-m2b-lcd5/init.sun8iw11p1.rc:root/init.sun8iw11p1.rc \
    device/softwinner/bpi-m2u-m2b-lcd5/init.recovery.sun8iw11p1.rc:root/init.recovery.sun8iw11p1.rc \
    device/softwinner/bpi-m2u-m2b-lcd5/ueventd.sun8iw11p1.rc:root/ueventd.sun8iw11p1.rc \
    device/softwinner/bpi-m2u-m2b-lcd5/recovery.fstab:recovery.fstab \
    device/softwinner/bpi-m2u-m2b-lcd5/modules/modules/nand.ko:root/nand.ko \
    device/softwinner/bpi-m2u-m2b-lcd5/modules/modules/gslX680new.ko:root/gslX680new.ko \
    device/softwinner/bpi-m2u-m2b-lcd5/modules/modules/sw-device.ko:root/sw-device.ko \
    device/softwinner/bpi-m2u-m2b-lcd5/modules/modules/sw-device.ko:obj/sw-device.ko \
    device/softwinner/bpi-m2u-m2b-lcd5/modules/modules/gslX680new.ko:obj/gslX680new.ko

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.xml:system/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:system/etc/permissions/android.software.verified_boot.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:system/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \

# Low mem(memory <= 512M) device should not copy android.software.managed_users.xml
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.software.managed_users.xml:system/etc/permissions/android.software.managed_users.xml

#  BPI-M2_Ultra-Berry 5LCD Touch KeyCode
PRODUCT_COPY_FILES += \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/camera.cfg:system/etc/camera.cfg \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/gsensor.cfg:system/usr/gsensor.cfg \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/media_profiles.xml:system/etc/media_profiles.xml \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/sunxi-keyboard.kl:system/usr/keylayout/sunxi-keyboard.kl \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/tp.idc:system/usr/idc/tp.idc \
    device/softwinner/bpi-m2u-m2b-lcd5/configs/gt9xxnew_ts.kl:system/usr/keylayout/gt9xxnew_ts.kl

# BPI-M2_Ultra-Berry
PRODUCT_COPY_FILES += \
   device/softwinner/bpi-m2u-m2b-lcd5/bluetooth/bt_vendor.conf:system/etc/bluetooth/bt_vendor.conf
	
# bootanimation
PRODUCT_COPY_FILES += \
    device/softwinner/bpi-m2u-m2b-lcd5/media/bootanimation.zip:system/media/bootanimation.zip

# camera config for camera detector
PRODUCT_COPY_FILES += \
    device/softwinner/bpi-m2u-m2b-lcd5/hawkview/sensor_list_cfg.ini:system/etc/hawkview/sensor_list_cfg.ini

# Radio Packages and Configuration Flie
$(call inherit-product, device/softwinner/common/rild/radio_common.mk)
#$(call inherit-product, device/softwinner/common/ril_modem/huawei/mu509/huawei_mu509.mk)
#$(call inherit-product, device/softwinner/common/ril_modem/Oviphone/em55/oviphone_em55.mk)

# BPI-M2_Ultra-Berry
# Realtek wifi efuse map
#PRODUCT_COPY_FILES += \
#    device/softwinner/bpi-m2u-m2b-lcd5/wifi_efuse_8723bs-vq0.map:system/etc/wifi/wifi_efuse_8723bs-vq0.map


PRODUCT_PROPERTY_OVERRIDES += \
    persist.sys.usb.config=adb \
    ro.adb.secure=0 \
    rw.logger=0

PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.heapsize=512m \
    dalvik.vm.heapstartsize=8m \
    dalvik.vm.heapgrowthlimit=192m \
    dalvik.vm.heaptargetutilization=0.75 \
    dalvik.vm.heapminfree=2m \
    dalvik.vm.heapmaxfree=8m 

PRODUCT_PROPERTY_OVERRIDES += \
    ro.zygote.disable_gl_preload=true

# BPI-M2_Ultra-Berry
PRODUCT_PROPERTY_OVERRIDES += \
    ro.sf.lcd_density=160 \
    ro.display.sdcard=1 \
    ro.part.sdcard=1

PRODUCT_PROPERTY_OVERRIDES += \
    ro.spk_dul.used=false \

# BPI-M2_Ultra-Berry
PRODUCT_PROPERTY_OVERRIDES += \
   	persist.sys.timezone=Asia/Taipei \
	persist.sys.language=EN \
	persist.sys.country=US

# stoarge
PRODUCT_PROPERTY_OVERRIDES += \
    persist.fw.force_adoptable=true
PRODUCT_CHARACTERISTICS := tablet

PRODUCT_AAPT_CONFIG := tvdpi xlarge hdpi xhdpi large
PRODUCT_AAPT_PREF_CONFIG := tvdpi

# BPI-M2_Ultra-Berry supports GMS
$(call inherit-product-if-exists, vendor/google/products/gms_base.mk)

PRODUCT_BRAND := BPI
PRODUCT_NAME := bpi_m2u_m2b_lcd5
PRODUCT_DEVICE := bpi-m2u-m2b-lcd5
PRODUCT_MODEL := BPI M2 Ultra
PRODUCT_MANUFACTURER := SINOVOIP
