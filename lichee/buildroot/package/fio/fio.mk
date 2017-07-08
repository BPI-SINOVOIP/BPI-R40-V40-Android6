################################################################################
#
# fio
#
################################################################################

FIO_VERSION = fio-2.1.10
FIO_SOURCE = fio-2.1.10.tar.gz 
FIO_SITE = git://git.kernel.dk/fio.git
FIO_LICENSE = GPLv2 + special obligations
FIO_LICENSE_FILES = LICENSE
FIO_INSTALL_STAGING = YES
FIO_INSTALL_TARGET = YES

define FIO_CONFIGURE_CMDS
	(cd $(@D); ./configure --cc="$(TARGET_CC)" --extra-cflags="$(TARGET_CFLAGS)")
endef

define FIO_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define FIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D $(@D)/fio $(TARGET_DIR)/usr/bin/fio
endef

#$(eval $(generic-package))
$(eval $(call AUTOTARGETS,package,fio))
