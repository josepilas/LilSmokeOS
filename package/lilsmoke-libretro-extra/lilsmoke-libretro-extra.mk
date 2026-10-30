LILSMOKE_LIBRETRO_EXTRA_VERSION = 0.1.0
LILSMOKE_LIBRETRO_EXTRA_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-libretro-extra
LILSMOKE_LIBRETRO_EXTRA_SITE_METHOD = local
# Individual engine licenses are retained alongside the separate payload.
LILSMOKE_LIBRETRO_EXTRA_LICENSE = Various
LILSMOKE_LIBRETRO_EXTRA_DEPENDENCIES = retroarch-mips host-python3
LILSMOKE_LIBRETRO_EXTRA_INSTALL_TARGET = NO
LILSMOKE_LIBRETRO_EXTRA_INSTALL_IMAGES = YES
LILSMOKE_LIBRETRO_EXTRA_CORES = nxengine prboom stella2014 prosystem atari800 race pokemini wswan picodrive quicknes snes9x2002 gpsp pcsx_rearmed
LILSMOKE_LIBRETRO_EXTRA_CFLAGS = $(TARGET_CFLAGS) -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -DHAVE_MIPS_DSP

define LILSMOKE_LIBRETRO_EXTRA_BUILD_CMDS
    $(TARGET_MAKE_ENV) CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
        AR="$(TARGET_AR)" RANLIB="$(TARGET_RANLIB)" STRIP="$(TARGET_STRIP)" \
        READELF="$(TARGET_READELF)" JOBS="$(PARALLEL_JOBS)" \
        CFLAGS="$(LILSMOKE_LIBRETRO_EXTRA_CFLAGS)" CXXFLAGS="$(LILSMOKE_LIBRETRO_EXTRA_CFLAGS)" \
        LDFLAGS="$(TARGET_LDFLAGS)" LILSMOKE_CORE_LOCK="$(@D)/cores.lock" \
        sh $(BR2_EXTERNAL_LILSMOKE_PATH)/package/retroarch-mips/build-cores.sh \
            $(@D)/sources $(@D)/built $(LILSMOKE_LIBRETRO_EXTRA_CORES)
endef

define LILSMOKE_LIBRETRO_EXTRA_INSTALL_IMAGES_CMDS
    $(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/emulators/stage-extra-cores.py \
        --built $(@D)/built --sources $(@D)/sources --lock $(@D)/cores.lock \
        --output $(BINARIES_DIR)/emulator-expansion
endef

$(eval $(generic-package))
