# Pins and native source patches are in sources.json/source-fixes.json.
LILSMOKE_OD_CONSOLES_VERSION = 20261008
LILSMOKE_OD_CONSOLES_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-od-consoles
LILSMOKE_OD_CONSOLES_SITE_METHOD = local
LILSMOKE_OD_CONSOLES_LICENSE = GPL-2.0+, GPL-3.0+, MAME
LILSMOKE_OD_CONSOLES_DEPENDENCIES = host-python3 sdl2 lilsmoke-sdl12 lilsmoke-od-sdl zlib libpng
LILSMOKE_OD_CONSOLES_INSTALL_TARGET = NO
LILSMOKE_OD_CONSOLES_INSTALL_IMAGES = YES
LILSMOKE_OD_CONSOLES_CACHE ?= /root/lilsmoke-emulator-expansion-20261008
LILSMOKE_OD_CONSOLES_ENGINES ?= fceu320 gnuboy prosystem stella atari800 oswan picodrive handy race pokemini regba
# Temper is built by the private-card recipe. Its upstream license needs
# clarification before including it in a generally redistributed image.

define LILSMOKE_OD_CONSOLES_BUILD_CMDS
	$(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/emulators/od-consoles/build.py \
		--repo $(BR2_EXTERNAL_LILSMOKE_PATH) --buildroot $(BASE_DIR) \
		--cache $(LILSMOKE_OD_CONSOLES_CACHE) --jobs 1 $(LILSMOKE_OD_CONSOLES_ENGINES)
endef

define LILSMOKE_OD_CONSOLES_INSTALL_IMAGES_CMDS
	mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators
	set -e; for engine in $(LILSMOKE_OD_CONSOLES_ENGINES); do \
		$(INSTALL) -D -m 0755 $(LILSMOKE_OD_CONSOLES_CACHE)/od-console-stage/storage/emulators/opendingux/$$engine \
			$(BINARIES_DIR)/emulator-expansion/emulators/opendingux/$$engine; \
		mkdir -p $(BINARIES_DIR)/emulator-expansion/emulators/share/$$engine; \
		cp -a $(LILSMOKE_OD_CONSOLES_CACHE)/od-console-stage/storage/emulators/share/$$engine/. \
			$(BINARIES_DIR)/emulator-expansion/emulators/share/$$engine/; \
	done
endef

$(eval $(generic-package))
