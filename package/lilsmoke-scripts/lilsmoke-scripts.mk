# lilsmoke-scripts — runtime helper scripts
#
# Currently the scripts are installed by post-build.sh from the board dir.
# This package exists so they can be toggled on/off via menuconfig and
# so future versions can ship binary tools (e.g. a small C wifi-config
# helper) without changing the architecture.
#
# In J6 we'll add the OTA unpacker and scraper scripts here.

LILSMOKE_SCRIPTS_VERSION = 0.1.0
LILSMOKE_SCRIPTS_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-scripts/src
LILSMOKE_SCRIPTS_SITE_METHOD = local
LILSMOKE_SCRIPTS_LICENSE = MPL-2.0
LILSMOKE_SCRIPTS_LICENSE_FILES = LICENSE
LILSMOKE_SCRIPTS_DEPENDENCIES = dropbear dosfstools exfatprogs e2fsprogs libarchive libsodium sdl sdl2 json-c

define LILSMOKE_SCRIPTS_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) -std=c11 -Wall -Wextra \
        -I$(STAGING_DIR)/usr/include/SDL2 \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-usb-gamepads/src \
        -c $(@D)/lilsmoke-treefrog-input.c -o $(@D)/treefrog-bridge.o
    $(TARGET_CXX) $(TARGET_CFLAGS) -std=c++17 -Wall -Wextra \
        -I$(STAGING_DIR)/usr/include/SDL2 \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src \
        -include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-od-sdl/src/input-wire.h \
        $(@D)/treefrog-bridge.o $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/treefrog-compat/profile-input.cpp \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/Input.cpp \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/Keymap.cpp \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/Log.cpp \
        $(TARGET_LDFLAGS) -lSDL2 -o $(@D)/lilsmoke-treefrog-input
    $(TARGET_CXX) $(TARGET_CFLAGS) -std=c++17 -Wall -Wextra \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-apps/src \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/treefrog-compat/preflight.cpp \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/TreeFrogCatalog.cpp \
        $(TARGET_LDFLAGS) -ljson-c -o $(@D)/lilsmoke-treefrog-check
    $(TARGET_CC) $(TARGET_CFLAGS) -std=c11 -Wall -Wextra -D_FILE_OFFSET_BITS=64 \
        $(@D)/lilsmoke-disk.c $(TARGET_LDFLAGS) -o $(@D)/lilsmoke-disk
    $(TARGET_CXX) $(TARGET_CFLAGS) -std=c++17 -Wall -Wextra \
        $(@D)/lilsmoke-rom.cpp $(TARGET_LDFLAGS) -larchive -lsodium -o $(@D)/lilsmoke-rom
    $(TARGET_CXX) $(TARGET_CFLAGS) -std=c++17 -Wall -Wextra \
        $(@D)/lilsmoke-emulators.cpp $(TARGET_LDFLAGS) -o $(@D)/lilsmoke-emulators
    $(TARGET_CXX) $(TARGET_CFLAGS) -std=c++17 -Wall -Wextra \
        $(@D)/lilsmoke-nx-data.cpp $(TARGET_LDFLAGS) -lsodium -o $(@D)/lilsmoke-nx-data
    $(TARGET_CC) $(TARGET_CFLAGS) -static -Os -std=c11 -Wall -Wextra \
        -I$(STAGING_DIR)/usr/include/SDL \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-safe-power.c \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-power-ui.c \
        $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-native-video.c -o $(@D)/m8-safe-power
endef

# Fixed IDs matter because /storage is mounted with uid/gid 1000 and survives
# firmware updates.  The default password is intentionally documented and is
# expected to be changed with passwd after first login.
LILSMOKE_SCRIPTS_USERS = lil 1000 lil 1000 =lilsmoke /persist/home/lil /bin/sh - LilSmokeOS remote user

define LILSMOKE_SCRIPTS_INSTALL_TARGET_CMDS
    # The stock service ignores SSH_ENABLED and would bypass our opt-in gate.
    rm -f $(TARGET_DIR)/etc/init.d/S50dropbear
    install -d $(TARGET_DIR)/usr/lib/lilsmoke
    install -m 0755 $(@D)/lilsmoke-treefrog-input $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(@D)/lilsmoke-treefrog-check $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-treefrog-launch $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-treefrog-port-check $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0644 $(LILSMOKE_SCRIPTS_SITE)/treefrog-keymap.txt $(TARGET_DIR)/usr/lib/lilsmoke/
    install -D -m 0755 $(LILSMOKE_SCRIPTS_SITE)/S03shm $(TARGET_DIR)/etc/init.d/S03shm
    install -m 0755 $(@D)/lilsmoke-disk $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(@D)/lilsmoke-rom $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(@D)/lilsmoke-emulators $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(@D)/lilsmoke-nx-data $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(@D)/m8-safe-power $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-storage $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-config-read $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-session-log $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-network $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-sshd $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-update     $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-wifi       $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-scrape     $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-launch     $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/m8-loading-run       $(TARGET_DIR)/usr/lib/lilsmoke/
    ln -sf ../lib/lilsmoke/lilsmoke-launch $(TARGET_DIR)/usr/bin/lilsmoke-launch
    ln -sf ../lib/lilsmoke/lilsmoke-emulators $(TARGET_DIR)/usr/bin/lilsmoke-emulators
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-od-launch  $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-power      $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-retroarch  $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-overclock  $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-zram       $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-hardware-probe $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-usb        $(TARGET_DIR)/usr/lib/lilsmoke/
    install -m 0755 $(LILSMOKE_SCRIPTS_SITE)/lilsmoke-network-info $(TARGET_DIR)/usr/bin/
    install -D -m 0755 $(LILSMOKE_SCRIPTS_SITE)/S25usb-gadget    $(TARGET_DIR)/etc/init.d/S25usb-gadget
endef

$(eval $(generic-package))
