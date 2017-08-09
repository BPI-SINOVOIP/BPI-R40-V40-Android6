###############################################################################
# GMS mandatory core packages
PRODUCT_PACKAGES := \
    ConfigUpdater \
    GoogleBackupTransport \
    GoogleFeedback \
    GoogleLoginService \
    GoogleOneTimeInitializer \
    GooglePackageInstaller \
    GooglePartnerSetup \
    GoogleServicesFramework \
    GoogleCalendarSyncAdapter \
    GoogleContactsSyncAdapter \
    GoogleTTS \
    GmsCore \
    Phonesky \
    SetupWizard \
    WebViewGoogle

# GMS mandatory libraries
PRODUCT_PACKAGES += \
    com.google.android.maps.jar \
    com.google.android.media.effects.jar

# Overlay For GMS devices
$(call inherit-product, device/sample/products/backup_overlay.mk)
$(call inherit-product, device/sample/products/location_overlay.mk)
PRODUCT_PACKAGE_OVERLAYS := vendor/google/products/gms_overlay

# Configuration files for GMS apps
PRODUCT_COPY_FILES := \
    vendor/google/etc/updatecmds/google_generic_update.txt:system/etc/updatecmds/google_generic_update.txt \
    vendor/google/etc/preferred-apps/google.xml:system/etc/preferred-apps/google.xml \
    vendor/google/etc/sysconfig/google.xml:system/etc/sysconfig/google.xml

# GMS mandatory application packages
PRODUCT_PACKAGES += \
    Chrome \
    Drive \
    Gmail2 \
    Hangouts \
    Maps \
    Music2 \
    Photos \
    Velvet \
    Videos \
    YouTube

# GMS optional application packages
PRODUCT_PACKAGES += \
    Books \
    CalendarGoogle \
    CloudPrint \
    DeskClockGoogle \
    DMAgent \
    FaceLock \
    GoogleHome \
    LatinImeGoogle \
    PlayGames \
    PlusOne \
    TagGoogle \
    talkback \
    AndroidPay

PRODUCT_PACKAGES += \
    EditorsDocs \
    EditorsSheets \
    EditorsSlides \
    Keep \
    Newsstand

#PRODUCT_PACKAGES += \
#    EditorsDocsStub \
#    EditorsSheetsStub \
#    EditorsSlidesStub \
#    KeepStub \
#    NewsstandStub

# More GMS optional application packages
PRODUCT_PACKAGES += \
    CalculatorGoogle \
    Bugle \
    GoogleHindiIME \
    GooglePinyinIME \
    JapaneseIME \
    KoreanIME \
    NewsWeather

# Overrides
PRODUCT_PROPERTY_OVERRIDES += \
    ro.setupwizard.mode=OPTIONAL \
    ro.com.google.gmsversion=6.0_r2
