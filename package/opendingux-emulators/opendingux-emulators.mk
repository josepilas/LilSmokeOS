# A tested source port, not an ABI loader for arbitrary uClibc OPK binaries.
OPENDINGUX_EMULATORS_VERSION = 6fff59072821a68859d0d4d5caeec36d02ea264f
OPENDINGUX_EMULATORS_SITE = https://github.com/retrofw/pocketsnes.git
OPENDINGUX_EMULATORS_SITE_METHOD = git
OPENDINGUX_EMULATORS_LICENSE = Snes9x, GPL-2.0+
OPENDINGUX_EMULATORS_LICENSE_FILES = src/include/snes9x.h
OPENDINGUX_EMULATORS_DEPENDENCIES = lilsmoke-sdl12 zlib libpng
OPENDINGUX_EMULATORS_CFLAGS = $(TARGET_CFLAGS)
ifeq ($(BR2_PACKAGE_OPENDINGUX_EMULATORS_DEBUG),y)
OPENDINGUX_EMULATORS_CFLAGS += -g
endif

define OPENDINGUX_EMULATORS_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f $(OPENDINGUX_EMULATORS_PKGDIR)/Makefile.lilsmoke \
        -j$(PARALLEL_JOBS) CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
        TARGET_CFLAGS="$(OPENDINGUX_EMULATORS_CFLAGS)" TARGET_LDFLAGS="$(TARGET_LDFLAGS)" \
        SDL_CFLAGS="-I$(STAGING_DIR)/usr/include/SDL" SDL_LIBS="-L$(STAGING_DIR)/usr/lib -lSDL"
endef

define OPENDINGUX_EMULATORS_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/pocketsnes/pocketsnes.dge \
        $(TARGET_DIR)/usr/lib/lilsmoke/od-emulators/pocketsnes
    $(INSTALL) -D -m 0644 $(@D)/pocketsnes/backdrop.png \
        $(TARGET_DIR)/usr/share/lilsmoke/pocketsnes/backdrop.png
    $(INSTALL) -D -m 0755 $(OPENDINGUX_EMULATORS_PKGDIR)/src/od-launch \
        $(TARGET_DIR)/usr/lib/lilsmoke/od-launch
endef

$(eval $(generic-package))
