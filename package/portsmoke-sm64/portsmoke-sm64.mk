################################################################################
# SM64 independent .lilpck development app (legacy build symbol retained)
################################################################################
PORTSMOKE_SM64_VERSION = 0.5.0
PORTSMOKE_SM64_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/portsmoke-sm64/src
PORTSMOKE_SM64_SITE_METHOD = local
PORTSMOKE_SM64_LICENSE = MPL-2.0 (original prototype code), CC0-1.0 (offset metadata), Public Domain (bitmap font), Zlib (SDL2), BSD-3-Clause (WebP)
PORTSMOKE_SM64_LICENSE_FILES = LICENSE LICENSE.metadata LICENSE.webp vendor/tinygl/LICENSE
PORTSMOKE_SM64_DEPENDENCIES = host-python3 sdl2 webp
PORTSMOKE_SM64_INSTALL_TARGET = NO
PORTSMOKE_SM64_INSTALL_IMAGES = YES

define PORTSMOKE_SM64_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -O2 -Wall -Wextra -Werror \
		$(@D)/main.cpp $(@D)/asset_pack.cpp $(@D)/runtime_assets.cpp \
		$(TARGET_LDFLAGS) -static-libstdc++ -static-libgcc -o $(@D)/portsmoke-sm64-data
	$(TARGET_STRIP) $(@D)/portsmoke-sm64-data
	$(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/portsmoke/sm64/build_scene.py \
		--source $(@D) --sdk $(BASE_DIR) --cc $(TARGET_CC) --cxx $(TARGET_CXX) --out $(@D)/scene
	$(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LILSMOKE_PATH)/tools/portsmoke/sm64/build_lilpck.py \
		--source $(@D) --binary $(@D)/portsmoke-sm64-data \
		--gcc-license-dir $(HOST_GCC_FINAL_DIR) \
		--scene $(@D)/scene/sm64-scene-dev --sdl-library $(TARGET_DIR)/usr/lib/libSDL2-2.0.so.0 \
		--sdl-license $(SDL2_DIR)/LICENSE.txt \
		--version $(PORTSMOKE_SM64_VERSION) \
		--output $(@D)/SM64-BYO-Data-Development-$(PORTSMOKE_SM64_VERSION).lilpck
endef

define PORTSMOKE_SM64_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 $(@D)/SM64-BYO-Data-Development-$(PORTSMOKE_SM64_VERSION).lilpck \
		$(BINARIES_DIR)/SM64-BYO-Data-Development-$(PORTSMOKE_SM64_VERSION).lilpck
endef

$(eval $(generic-package))
