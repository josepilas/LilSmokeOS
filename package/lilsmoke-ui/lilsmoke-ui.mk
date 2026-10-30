# lilsmoke-ui — custom C++17 SDL2/DirectFB frontend for LilSmokeOS
#
# Builds a single ~700 KB stripped binary that runs on /dev/fb0 via
# SDL2's DirectFB backend. Uses SDL2_ttf for text rendering and SDL2_image
# for PNG/JPEG/WebP loading (cover art, theme assets).

LILSMOKE_UI_VERSION = 0.1.0
LILSMOKE_UI_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src
LILSMOKE_UI_SITE_METHOD = local
LILSMOKE_UI_LICENSE = MPL-2.0
LILSMOKE_UI_LICENSE_FILES = LICENSE
LILSMOKE_UI_INSTALL_STAGING = YES
LILSMOKE_UI_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf directfb libpng webp zlib freetype json-c lilsmoke-apps

# Build flags
# - C++17, optimized for size
# - Link against SDL2/DirectFB (no GL, no X11)
LILSMOKE_UI_CXXFLAGS = $(TARGET_CXXFLAGS) -std=c++17 -O3 -flto -Wall \
    -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-scripts/src \
    -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-media/src \
    -ffunction-sections -fdata-sections \
    $(shell $(PKG_CONFIG_HOST_BINARY) --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null)
LILSMOKE_UI_LDFLAGS = $(TARGET_LDFLAGS) -flto -Wl,--gc-sections -s \
    $(shell $(PKG_CONFIG_HOST_BINARY) --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null) -lz -ljson-c -lwebp

define LILSMOKE_UI_BUILD_CMDS
    # Compile each .cpp → .o
    cd $(@D) && \
    for src in src/*.cpp; do \
        obj=$${src%.cpp}.o; \
        $(TARGET_CXX) $(LILSMOKE_UI_CXXFLAGS) -c -o $$obj $$src || exit 1; \
    done
    # Link
    cd $(@D) && \
    $(TARGET_CXX) $(LILSMOKE_UI_LDFLAGS) -o lilsmoke-ui src/*.o
    cd $(@D) && $(TARGET_CXX) $(LILSMOKE_UI_CXXFLAGS) -Isrc \
        apps/session.cpp src/GameSession.o src/GameHistory.o -Wl,--gc-sections -s -o lilsmoke-session
    cd $(@D) && $(TARGET_CXX) $(LILSMOKE_UI_CXXFLAGS) -Isrc \
        apps/keyboard.cpp src/VirtualKeyboard.o src/Renderer.o src/Font.o \
        src/Input.o src/Keymap.o src/Log.o $(LILSMOKE_UI_LDFLAGS) -o lilsmoke-keyboard
endef

define LILSMOKE_UI_INSTALL_STAGING_CMDS
    $(INSTALL) -D -m 0644 $(@D)/include/lilsmoke/keyboard.h \
        $(STAGING_DIR)/usr/include/lilsmoke/keyboard.h
endef

define LILSMOKE_UI_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-ui $(TARGET_DIR)/usr/bin/lilsmoke-ui
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-session $(TARGET_DIR)/usr/lib/lilsmoke/lilsmoke-session
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-keyboard $(TARGET_DIR)/usr/bin/lilsmoke-keyboard
    $(INSTALL) -D -m 0644 $(@D)/assets/lilsmoke-cursors.webp \
        $(TARGET_DIR)/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp
    # Install default theme assets (logo.png already installed by post-build.sh)
    # In J3 we keep using board/qemu-mipsel/theme.xml — no new theme assets needed
endef

$(eval $(generic-package))
