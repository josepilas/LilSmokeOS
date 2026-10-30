LILSMOKE_OD_ENGINES_VERSION = 1
LILSMOKE_OD_ENGINES_SITE = $(patsubst %/,%,$(LILSMOKE_OD_ENGINES_PKGDIR))
LILSMOKE_OD_ENGINES_SITE_METHOD = local
LILSMOKE_OD_ENGINES_LICENSE = Various
LILSMOKE_OD_ENGINES_DEPENDENCIES = lilsmoke-od-sdl sdl2 sdl2_image sdl2_mixer \
	libpng jpeg freetype pixman fmt host-cmake host-pkgconf host-python3
LILSMOKE_OD_ENGINES_DRIVER = $(abspath $(LILSMOKE_OD_ENGINES_PKGDIR)/../../tools/emulators/od-engines/build.sh)
LILSMOKE_OD_ENGINES_BUILD_STEPS ?= prepare deps liblcf nxengine prboom easyrpg scummvm mgba openbor easyrpg-core scummvm-core
ifeq ($(BR2_PACKAGE_LILSMOKE_OD_MAME4ALL),y)
LILSMOKE_OD_ENGINES_BUILD_STEPS += mame4all
endif
LILSMOKE_OD_ENGINES_INSTALL_TARGET = NO
LILSMOKE_OD_ENGINES_INSTALL_IMAGES = YES

define LILSMOKE_OD_ENGINES_BUILD_CMDS
	set -e; for step in $(LILSMOKE_OD_ENGINES_BUILD_STEPS); do \
		OD_BUILD_ROOT="$(@D)/native" OD_SDK="$(HOST_DIR)" OD_JOBS=1 \
			bash "$(LILSMOKE_OD_ENGINES_DRIVER)" $$step; \
	done
endef

define LILSMOKE_OD_ENGINES_INSTALL_IMAGES_CMDS
	$(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/emulators/od-engines/audit-stage.py \
		--root $(@D)/native --sdk $(HOST_DIR) --copy-libraries
	mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators/opendingux/data/share
	cp -a $(@D)/native/built-native/bin/. $(BINARIES_DIR)/emulator-expansion/emulators/opendingux/
	cp -a $(@D)/native/built-native/data/share/. $(BINARIES_DIR)/emulator-expansion/emulators/opendingux/data/share/
	mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators/licenses
	cp -a $(@D)/native/built-native/licenses/. $(BINARIES_DIR)/emulator-expansion/emulators/licenses/
	mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators/lib
	cp -a $(@D)/native/built-native/lib/. $(BINARIES_DIR)/emulator-expansion/emulators/lib/
	mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators/libretro
	cp -a $(@D)/native/built-libretro/. $(BINARIES_DIR)/emulator-expansion/emulators/libretro/
	mkdir -p $(BINARIES_DIR)/emulator-expansion/bios/scummvm/extra $(BINARIES_DIR)/emulator-expansion/bios/scummvm/theme
	cp $(@D)/native/built-native/data/share/scummvm/*.dat $(@D)/native/built-native/data/share/scummvm/*.tbl \
		$(@D)/native/built-native/data/share/scummvm/*.cpt $(BINARIES_DIR)/emulator-expansion/bios/scummvm/extra/
	cp $(@D)/native/built-native/data/share/scummvm/scummclassic.zip \
		$(BINARIES_DIR)/emulator-expansion/bios/scummvm/theme/
endef

$(eval $(generic-package))
