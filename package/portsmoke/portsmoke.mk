################################################################################
#
# portsmoke
#
################################################################################

PORTSMOKE_VERSION = 0.1.0
PORTSMOKE_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/portsmoke/src
PORTSMOKE_SITE_METHOD = local
PORTSMOKE_LICENSE = MPL-2.0
PORTSMOKE_LICENSE_FILES = LICENSE

define PORTSMOKE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/portsmoke-launch $(TARGET_DIR)/usr/bin/portsmoke-launch
	$(INSTALL) -D -m 0755 $(@D)/S11portsmoke $(TARGET_DIR)/etc/init.d/S11portsmoke
	$(INSTALL) -d $(TARGET_DIR)/storage/portsmoke/engines
	$(INSTALL) -d $(TARGET_DIR)/storage/portsmoke/ports
	$(INSTALL) -d $(TARGET_DIR)/storage/portsmoke/saves
	$(INSTALL) -d $(TARGET_DIR)/mnt/portsmoke
endef

$(eval $(generic-package))
