LILSMOKE_USB_TRANSPORT_VERSION = 1.0.26
LILSMOKE_USB_TRANSPORT_SOURCE = libusb-$(LILSMOKE_USB_TRANSPORT_VERSION).tar.bz2
LILSMOKE_USB_TRANSPORT_SITE = https://github.com/libusb/libusb/releases/download/v$(LILSMOKE_USB_TRANSPORT_VERSION)
LILSMOKE_USB_TRANSPORT_LICENSE = LGPL-2.1+
LILSMOKE_USB_TRANSPORT_LICENSE_FILES = COPYING
LILSMOKE_USB_TRANSPORT_CONF_OPTS = --disable-static --disable-udev --disable-eventfd --disable-timerfd
define LILSMOKE_USB_TRANSPORT_PATCH
    $(APPLY_PATCHES) $(@D) $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/portable-usb '*.patch'
    $(INSTALL) -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/portable-usb/poll_hotplug.h $(@D)/libusb/os/
    $(INSTALL) -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/portable-usb/poll_hotplug.inc $(@D)/libusb/os/
endef
LILSMOKE_USB_TRANSPORT_POST_PATCH_HOOKS += LILSMOKE_USB_TRANSPORT_PATCH
define LILSMOKE_USB_TRANSPORT_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/libusb/.libs/libusb-1.0.so.0.3.0 \
        $(TARGET_DIR)/usr/lib/lilsmoke-usb/libusb-1.0.so.0
endef
$(eval $(autotools-package))
