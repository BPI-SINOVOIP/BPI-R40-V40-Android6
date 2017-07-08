MTOP_DIR := $(BUILD_DIR)/mtop

$(MTOP_DIR)/.source :
	mkdir -pv $(MTOP_DIR)
	cp -vrf package/mtop/mtop.sh $(MTOP_DIR)
	touch $@

$(MTOP_DIR)/.configured : $(MTOP_DIR)/.source
	touch $@

define MTOP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0655 $(MTOP_DIR)/mtop.sh $(TARGET_DIR)/usr/bin/
	(cd $(TARGET_DIR)/usr/bin; ln -sf mtop.sh mtop)
endef

define MTOP_UNINSTALL_TARGET_CMDS
	rm -f $(TARGET_DIR)/usr/bin/mtop
	rm -f $(TARGET_DIR)/usr/bin/mtop.sh
endef


mtop: $(MTOP_DIR)/.configured
	$(MTOP_INSTALL_TARGET_CMDS)

mtop-clean:
	rm -fr $(MTOP_DIR)/


##############################################################
#
# Add our target
#
#############################################################
ifeq ($(BR2_PACKAGE_MTOP),y)
TARGETS += mtop
endif
