LILSMOKE_GAMEPADS_VERSION = cd236b089ae6dbfb4b896f60d1850deaf5beccbf
LILSMOKE_GAMEPADS_SITE = https://github.com/libretro/retroarch-joypad-autoconfig.git
LILSMOKE_GAMEPADS_SITE_METHOD = git
LILSMOKE_GAMEPADS_LICENSE = MIT
LILSMOKE_GAMEPADS_LICENSE_FILES = COPYING

define LILSMOKE_GAMEPADS_INSTALL_TARGET_CMDS
    mkdir -p $(TARGET_DIR)/usr/share/lilsmoke/gamepads
    cp -r $(@D)/linuxraw $(@D)/sdl2 $(TARGET_DIR)/usr/share/lilsmoke/gamepads/
    $(INSTALL) -D -m 0644 $(@D)/COPYING $(TARGET_DIR)/usr/share/lilsmoke/gamepads/COPYING
endef

$(eval $(generic-package))
