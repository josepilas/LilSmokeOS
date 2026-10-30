LILSMOKE_BLUETOOTH_VERSION = 1.0
LILSMOKE_BLUETOOTH_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-bluetooth/src
LILSMOKE_BLUETOOTH_SITE_METHOD = local
LILSMOKE_BLUETOOTH_LICENSE = MPL-2.0
LILSMOKE_BLUETOOTH_LICENSE_FILES = LICENSE
LILSMOKE_BLUETOOTH_DEPENDENCIES = bluez5_utils bluez-alsa libglib2 host-pkgconf

define LILSMOKE_BLUETOOTH_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -Wall -Wextra \
		$(shell $(PKG_CONFIG_HOST_BINARY) --cflags gio-2.0 gio-unix-2.0) \
		$(@D)/lilsmoke-bt.cpp -o $(@D)/lilsmoke-bt \
		$(TARGET_LDFLAGS) $(shell $(PKG_CONFIG_HOST_BINARY) --libs gio-2.0 gio-unix-2.0)
endef

define LILSMOKE_BLUETOOTH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/lilsmoke-bt $(TARGET_DIR)/usr/bin/lilsmoke-bt
	$(INSTALL) -D -m 0755 $(@D)/S40bluetoothd $(TARGET_DIR)/etc/init.d/S40bluetoothd
	$(INSTALL) -D -m 0644 $(@D)/main.conf $(TARGET_DIR)/etc/bluetooth/main.conf
endef

$(eval $(generic-package))
