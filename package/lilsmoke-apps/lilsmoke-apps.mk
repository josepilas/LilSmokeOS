LILSMOKE_APPS_VERSION = 1
LILSMOKE_APPS_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-apps/src
LILSMOKE_APPS_SITE_METHOD = local
LILSMOKE_APPS_LICENSE = MPL-2.0
LILSMOKE_APPS_LICENSE_FILES = LICENSE
LILSMOKE_APPS_DEPENDENCIES = libarchive json-c libsodium
define LILSMOKE_APPS_BUILD_CMDS
    $(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -O2 -Wall -Wextra \
        -I$(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-scripts/src \
        $(@D)/lilsmoke-apps.cpp $(TARGET_LDFLAGS) -larchive -ljson-c -lsodium -o $(@D)/lilsmoke-apps
endef
define LILSMOKE_APPS_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-apps $(TARGET_DIR)/usr/lib/lilsmoke/lilsmoke-apps
    $(INSTALL) -d $(TARGET_DIR)/storage/independent-apps
    $(INSTALL) -D -m 0755 $(LILSMOKE_APPS_PKGDIR)/src/S08applications $(TARGET_DIR)/etc/init.d/S08applications
endef
$(eval $(generic-package))
