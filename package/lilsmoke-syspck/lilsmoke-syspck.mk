LILSMOKE_SYSPCK_VERSION = 1.0
LILSMOKE_SYSPCK_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/lilsmoke-syspck/src
LILSMOKE_SYSPCK_SITE_METHOD = local
LILSMOKE_SYSPCK_LICENSE = MPL-2.0
LILSMOKE_SYSPCK_LICENSE_FILES = LICENSE
LILSMOKE_SYSPCK_DEPENDENCIES = libarchive xz dtc libsodium lilsmoke-scripts

define LILSMOKE_SYSPCK_BUILD_CMDS
    $(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -Os -Wall -Wextra \
        $(@D)/syspck.cpp $(TARGET_LDFLAGS) -larchive -lfdt -lsodium -o $(@D)/lilsmoke-syspck
endef

define LILSMOKE_SYSPCK_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/lilsmoke-syspck $(TARGET_DIR)/usr/bin/lilsmoke-syspck
    $(INSTALL) -D -m 0755 $(LILSMOKE_SYSPCK_SITE)/S08syspck $(TARGET_DIR)/etc/init.d/S08syspck
    $(INSTALL) -d -m 0755 $(TARGET_DIR)/etc/lilsmoke/syspck-keys
    $(INSTALL) -d -m 0755 $(TARGET_DIR)/usr/share/lilsmoke-syspck/empty
    # No development signing key is trusted in a shipped image.
endef

$(eval $(generic-package))
