LILSMOKE_OPENDINGUX_VERSION = 1
LILSMOKE_OPENDINGUX_SITE = $(patsubst %/,%,$(LILSMOKE_OPENDINGUX_PKGDIR))
LILSMOKE_OPENDINGUX_SITE_METHOD = local
LILSMOKE_OPENDINGUX_LICENSE = MPL-2.0, LGPL-2.1+ (libini)
LILSMOKE_OPENDINGUX_LICENSE_FILES = LICENSE vendor/libini/LICENSE.txt
LILSMOKE_OPENDINGUX_DEPENDENCIES = lilsmoke-sqfs libsodium
define LILSMOKE_OPENDINGUX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
		SQFS_CFLAGS="-I$(STAGING_DIR)/usr/include" \
		SQFS_LIBS="-L$(STAGING_DIR)/usr/lib -lsquashfs"
endef
define LILSMOKE_OPENDINGUX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/lilsmoke-opendingux $(TARGET_DIR)/usr/lib/lilsmoke/lilsmoke-opendingux
	$(INSTALL) -D -m 0755 $(@D)/libini.so.0 $(TARGET_DIR)/usr/lib/libini.so.0
	$(INSTALL) -D -m 0644 $(@D)/pocketsnes.desktop $(TARGET_DIR)/usr/share/lilsmoke/opendingux/apps/pocketsnes.desktop
endef
$(eval $(generic-package))
