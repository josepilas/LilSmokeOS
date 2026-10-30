# Build both the reconstructed open-source core and the native SDL2 frontend.
PCSX4ALL_VERSION = 0.1.0
PCSX4ALL_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/pcsx4all/src
PCSX4ALL_SITE_METHOD = local
PCSX4ALL_LICENSE = GPL-2.0+
PCSX4ALL_LICENSE_FILES = gpl.txt
PCSX4ALL_DEPENDENCIES = zlib libpng sdl2 sdl2_ttf lilsmoke-apps host-python3

PCSX4ALL_INPUT_SRC = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src
PCSX4ALL_CXXFLAGS = $(TARGET_CXXFLAGS) -std=c++17 -O3 -fno-delete-null-pointer-checks -mips32r2 -mplt -mno-shared \
    -include $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-od-sdl/src/input-wire.h \
    -I$(@D)/src/port/rkgame -I$(PCSX4ALL_INPUT_SRC) \
    -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-scripts/src \
    $(shell $(PKG_CONFIG_HOST_BINARY) --cflags sdl2)
PCSX4ALL_LDFLAGS = $(TARGET_LDFLAGS) -no-pie -Wl,-Ttext-segment=0x40000000 \
    $(shell $(PKG_CONFIG_HOST_BINARY) --libs sdl2 SDL2_ttf) -lpng -lz -lm -lpthread -lrt

# This SDK's audio-qualified engine must not be replaced by a stale local copy.
PCSX4ALL_QUALIFIED_CORE ?= /root/lilsmoke-psx-20261010/native/libpcsx4all_rkgame.a
ifneq ($(wildcard $(PCSX4ALL_QUALIFIED_CORE)),)
PCSX4ALL_CORE_ARCHIVE = $(PCSX4ALL_QUALIFIED_CORE)
define PCSX4ALL_BUILD_CORE_CMDS
    printf '%s  %s\n' '1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a' \
        '$(PCSX4ALL_CORE_ARCHIVE)' | sha256sum -c -
endef
else
PCSX4ALL_CORE_ARCHIVE = $(@D)/libpcsx4all_rkgame.a
define PCSX4ALL_BUILD_CORE_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.rkgame NO_SDL=1 clean
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.rkgame -j$(PARALLEL_JOBS) \
        CROSS_COMPILE="$(TARGET_CROSS)" NO_SDL=1 SOUND_DRIVERS=sdl2 \
        EXTRA_CFLAGS="$(TARGET_CFLAGS) -O3 -g0 -fno-delete-null-pointer-checks"
endef
endif

define PCSX4ALL_BUILD_CMDS
    $(PCSX4ALL_BUILD_CORE_CMDS)
    $(TARGET_CXX) $(PCSX4ALL_CXXFLAGS) \
        $(@D)/pcsx4all-launch.cpp $(@D)/native-menu.cpp $(PCSX4ALL_INPUT_SRC)/Input.cpp \
        $(PCSX4ALL_INPUT_SRC)/Keymap.cpp $(PCSX4ALL_INPUT_SRC)/Log.cpp \
        $(PCSX4ALL_CORE_ARCHIVE) $(PCSX4ALL_LDFLAGS) -o $(@D)/pcsx4all-launch
endef

define PCSX4ALL_INSTALL_TARGET_CMDS
    $(INSTALL) -d $(TARGET_DIR)/usr/share/lilsmoke/builtin-apps
    $(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/apps/build_pcsx4all.py \
        --binary $(@D)/pcsx4all-launch --target-root $(TARGET_DIR) --readelf $(TARGET_READELF) \
        --core-archive $(PCSX4ALL_CORE_ARCHIVE) \
        --output $(@D)/PCSX4ALL-1.0.6.lilpck --replace
    $(INSTALL) -m 0644 $(@D)/PCSX4ALL-1.0.6.lilpck $(TARGET_DIR)/usr/share/lilsmoke/builtin-apps/
    $(INSTALL) -D -m 0644 $(PCSX4ALL_PKGDIR)/pcsx4all.cfg $(TARGET_DIR)/etc/pcsx4all.cfg
    $(INSTALL) -d $(TARGET_DIR)/persist/bios $(TARGET_DIR)/persist/saves/pcsx4all \
        $(TARGET_DIR)/persist/states/pcsx4all $(TARGET_DIR)/persist/config/pcsx4all
endef

$(eval $(generic-package))
