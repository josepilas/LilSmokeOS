# lilsmoke-fbtest — tiny framebuffer smoke test for J1
#
# Builds a single static binary (no .so dependencies, ~12 KB stripped)
# that paints the LilSmokeOS logo to /dev/fb0 via mmap.

LILSMOKE_FBTEST_VERSION = 0.1.0
LILSMOKE_FBTEST_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-fbtest/src
LILSMOKE_FBTEST_SITE_METHOD = local
LILSMOKE_FBTEST_LICENSE = MPL-2.0
LILSMOKE_FBTEST_LICENSE_FILES = LICENSE
LILSMOKE_FBTEST_DEPENDENCIES = libpng zlib

define LILSMOKE_FBTEST_BUILD_CMDS
    $(TARGET_CC) $(TARGET_CFLAGS) -O2 -Wall -s \
        -I$(STAGING_DIR)/usr/include \
        $(LILSMOKE_FBTEST_SITE)/lilsmoke-fbtest.c \
        -o $(@D)/lilsmoke-fbtest \
        -L$(STAGING_DIR)/usr/lib -lpng -lz -lm
endef

define LILSMOKE_FBTEST_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-fbtest \
        $(TARGET_DIR)/usr/bin/lilsmoke-fbtest
endef

$(eval $(generic-package))
