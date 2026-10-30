# retroarch-mips — RetroArch + 8 curated libretro cores for LilSmokeOS
#
# This package:
#   1. Builds RetroArch from source (git tag v1.19.1) with DirectFB video
#   2. Builds 8 curated libretro cores from source
#   3. Installs RetroArch binary + cores + a default retroarch.cfg
#
# Core revisions and output names are pinned in cores.lock. The build helper
# uses each upstream build system with the Buildroot target toolchain.

RETROARCH_MIPS_VERSION = v1.19.1
RETROARCH_MIPS_SITE = https://github.com/libretro/RetroArch.git
RETROARCH_MIPS_SITE_METHOD = git
RETROARCH_MIPS_GIT_SUBMODULES = NO
RETROARCH_MIPS_LICENSE = GPLv3+
RETROARCH_MIPS_DEPENDENCIES = alsa-lib freetype libpng zlib libevdev sdl2 host-cmake

# Cross-compile flags. Real hardware gets MIPS32r2 tuning; VBox keeps
# the same DirectFB/no-GPU profile without impossible MIPS-only flags.
RETROARCH_MIPS_ARCH_CFLAGS = -DLSB_FIRST
ifeq ($(BR2_mipsel),y)
RETROARCH_MIPS_ARCH_CFLAGS += -mips32r2 -mtune=74kc -mdspr2 -mfp32 \
    -mhard-float -mplt -DHAVE_MIPS_DSP
endif
RETROARCH_MIPS_CFLAGS = $(TARGET_CFLAGS) $(RETROARCH_MIPS_ARCH_CFLAGS)

# Configure: SDL2/DirectFB video, ALSA audio, no OpenGL
RETROARCH_MIPS_CONF_OPTS = \
    --enable-sdl2 \
    --enable-alsa \
    --disable-oss \
    --disable-pulse \
    --disable-jack \
    --disable-opengl \
    --disable-opengles \
    --disable-opengl_core \
    --disable-vulkan \
    --disable-vulkan_display \
    --disable-glslang \
    --disable-builtinglslang \
    --disable-x11 \
    --disable-wayland \
    --disable-kms \
    --disable-crtswitchres \
    --disable-materialui \
    --disable-xmb \
    --disable-ozone \
    --disable-networking \
    --disable-libretrodb \
    --disable-cheevos \
    --enable-freetype \
    --enable-zlib \
    --disable-builtinzlib \
    --disable-flac \
    --disable-builtinflac \
    --disable-ffmpeg \
    --disable-sdl \
    --disable-discord \
    --disable-ssl \
    --disable-systemd \
    --disable-udev

# --- Cores ---
# Each core is a separate git repo cloned during the build step.
# We pin specific commits to ensure reproducibility.
LILSMOKE_CORES_DIR = $(TARGET_DIR)/usr/lib/libretro
# Revisions, makefiles and canonical output names live in cores.lock.
# A persisted SNES QA switch must not silently strip a user release to one core.
LILSMOKE_CORES = fceumm gambatte mgba snes9x2005 genesis_plus_gx

ifeq ($(BR2_PACKAGE_LILSMOKE_LITE),y)
else
LILSMOKE_CORES += fbneo mednafen_pce_fast handy
endif

# --- Build RetroArch itself ---
define RETROARCH_MIPS_BUILD_CMDS
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_lilsmoke_input.c $(@D)/input/drivers_joypad/m8_lilsmoke_input.c
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_lilsmoke_input.h $(@D)/input/drivers_joypad/m8_lilsmoke_input.h
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-ui/src/src/gamepad-state.h $(@D)/input/drivers_joypad/gamepad-state.h
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_sdl2_window.h $(@D)/gfx/drivers/m8_sdl2_window.h
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_lilsmoke_audio.c $(@D)/audio/drivers/m8_lilsmoke_audio.c
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/opendingux-emulators/m8_pocketsnes_audio.c $(@D)/audio/drivers/m8_pocketsnes_audio.c
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/opendingux-emulators/m8_pocketsnes_audio.h $(@D)/audio/drivers/m8_pocketsnes_audio.h
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-audio.c $(@D)/audio/drivers/m8-audio.c
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/board/segam-m8-v6-stock/m8-audio.h $(@D)/audio/drivers/m8-audio.h
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_native_frame.h $(@D)/gfx/drivers/m8_native_frame.h
    $(INSTALL) -D -m 0644 $(RETROARCH_MIPS_PKGDIR)/m8_content_resolver.h $(@D)/tasks/m8_content_resolver.h
    cd $(@D) && \
    CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
    AR="$(TARGET_AR)" RANLIB="$(TARGET_RANLIB)" STRIP="$(TARGET_STRIP)" \
    CFLAGS="$(RETROARCH_MIPS_CFLAGS)" \
    CXXFLAGS="$(RETROARCH_MIPS_CFLAGS)" \
    LDFLAGS="$(TARGET_LDFLAGS)" \
    PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
    PKG_CONF_PATH="$(PKG_CONFIG_HOST_BINARY)" \
    PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig:$(STAGING_DIR)/usr/share/pkgconfig" \
    SDL2_CONFIG="$(STAGING_DIR)/usr/bin/sdl2-config" \
    ./configure --host=$(GNU_TARGET_NAME) $(RETROARCH_MIPS_CONF_OPTS) --prefix=/usr
    cd $(@D) && make -j$(PARALLEL_JOBS)
endef

# --- Build & install cores ---
define RETROARCH_MIPS_BUILD_CORES
    $(TARGET_MAKE_ENV) CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
        AR="$(TARGET_AR)" RANLIB="$(TARGET_RANLIB)" STRIP="$(TARGET_STRIP)" \
        READELF="$(TARGET_READELF)" JOBS="$(PARALLEL_JOBS)" \
        CFLAGS="$(RETROARCH_MIPS_CFLAGS)" CXXFLAGS="$(RETROARCH_MIPS_CFLAGS)" \
        LDFLAGS="$(TARGET_LDFLAGS)" \
        sh $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/build-cores.sh \
            $(@D)/pinned-cores $(@D)/built-cores $(LILSMOKE_CORES)
endef

define RETROARCH_MIPS_INSTALL_TARGET_CMDS
    # Install retroarch binary
    $(INSTALL) -D -m 0755 $(@D)/retroarch $(TARGET_DIR)/usr/bin/retroarch
    # Install default config
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/retroarch.cfg \
        $(TARGET_DIR)/etc/retroarch.cfg
    # Install cores
    mkdir -p $(LILSMOKE_CORES_DIR)
    set -e; \
    for entry in $(LILSMOKE_CORES); do \
        name=`awk -F '|' -v core="$$entry" '$$1 == core {print $$6}' \
            $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/cores.lock`; \
        test -n "$$name"; \
        $(INSTALL) -D -m 0644 "$(@D)/built-cores/$$name" "$(LILSMOKE_CORES_DIR)/$$name"; \
        $(INSTALL) -D -m 0644 "$(@D)/built-cores/$$name.build-info" \
            "$(TARGET_DIR)/usr/share/lilsmoke/core-builds/$$name.build-info"; \
        info=`awk -F '|' -v core="$$entry" '$$1 == core {print $$5}' \
            $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/cores.lock`; \
        $(INSTALL) -D -m 0644 \
            "$(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/core-info/$${info%.so}.info" \
            "$(TARGET_DIR)/usr/share/libretro/info/$${name%.so}.info"; \
    done
    $(INSTALL) -D -m 0644 $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/core-info/COPYING \
        $(TARGET_DIR)/usr/share/libretro/info/COPYING
endef

# Hook the cores build between RetroArch build and install
RETROARCH_MIPS_POST_BUILD_HOOKS += RETROARCH_MIPS_BUILD_CORES

$(eval $(generic-package))
