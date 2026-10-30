LILSMOKE_USB_GAMEPADS_VERSION = 0.1.0
LILSMOKE_USB_GAMEPADS_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/src
LILSMOKE_USB_GAMEPADS_SITE_METHOD = local
LILSMOKE_USB_GAMEPADS_LICENSE = MPL-2.0
LILSMOKE_USB_GAMEPADS_LICENSE_FILES = LICENSE
LILSMOKE_USB_GAMEPADS_DEPENDENCIES = sdl2 libusb lilsmoke-usb-transport
ifeq ($(BR2_PACKAGE_LILSMOKE_USB_GAMEPADS),y)
SDL2_DEPENDENCIES += libusb
SDL2_CONF_OPTS := $(filter-out --disable-hidapi-joystick --disable-hidapi-libusb,$(SDL2_CONF_OPTS))
SDL2_CONF_OPTS += --enable-hidapi-joystick --enable-hidapi-libusb
define LILSMOKE_USB_GAMEPADS_PATCH_DISCOVERY
    $(APPLY_PATCHES) $(@D) $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/sdl-input 0001-sdl-hidapi-usbfs-poll.patch
endef
SDL2_POST_PATCH_HOOKS += LILSMOKE_USB_GAMEPADS_PATCH_DISCOVERY
define LILSMOKE_USB_GAMEPADS_FIX_SDL2_STAGING_METADATA
    $(SED) 's|$(STAGING_DIR)/usr/include/directfb|$${includedir}/directfb|g' \
        -e 's|$(BASE_DIR)/staging/usr/include/directfb|$${includedir}/directfb|g' \
        $(STAGING_DIR)/usr/lib/pkgconfig/sdl2.pc
    $(SED) 's|$(STAGING_DIR)/usr/include/directfb|$${prefix}/include/directfb|g' \
        -e 's|$(BASE_DIR)/staging/usr/include/directfb|$${prefix}/include/directfb|g' \
        $(STAGING_DIR)/usr/bin/sdl2-config
endef
SDL2_POST_INSTALL_STAGING_HOOKS += LILSMOKE_USB_GAMEPADS_FIX_SDL2_STAGING_METADATA
endif
define LILSMOKE_USB_GAMEPADS_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) -std=gnu11 -O2 -Wall -Wextra \
        -I$(STAGING_DIR)/usr/include/SDL2 -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src \
        $(@D)/usb-gamepads.c -lSDL2 -o $(@D)/lilsmoke-usb-gamepads
    $(TARGET_CC) $(TARGET_CFLAGS) -std=gnu11 -O2 -I$(STAGING_DIR)/usr/include/libusb-1.0 \
        $(@D)/usb-probe.c -lusb-1.0 -o $(@D)/lilsmoke-usb-probe
endef
define LILSMOKE_USB_GAMEPADS_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-usb-gamepads $(TARGET_DIR)/usr/bin/lilsmoke-usb-gamepads
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-usb-probe $(TARGET_DIR)/usr/bin/lilsmoke-usb-probe
    $(INSTALL) -D -m 0755 $(LILSMOKE_USB_GAMEPADS_PKGDIR)/S07usb-gamepads $(TARGET_DIR)/etc/init.d/S07usb-gamepads
    $(INSTALL) -D -m 0755 $(LILSMOKE_USB_GAMEPADS_PKGDIR)/src/lilsmoke-input-diagnostics $(TARGET_DIR)/usr/bin/lilsmoke-input-diagnostics
endef
$(eval $(generic-package))
