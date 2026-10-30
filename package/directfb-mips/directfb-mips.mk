# directfb-mips — thin wrapper that pulls in Buildroot's stock DirectFB
# with the LilSmokeOS framebuffer-only profile.
#
# The actual DirectFB build is done by Buildroot's package/directfb/.
# This package exists so we can:
#   1. Toggle it on/off via menuconfig as a LilSmokeOS feature
#   2. Ship a custom /etc/directfbrc (installed by post-build.sh from board/)
#   3. In J4+, apply LilSmokeOS-specific patches to the DirectFB source

DIRECTFB_MIPS_VERSION = 1.7.7
DIRECTFB_MIPS_SITE = $(BR2_EXTERNAL_LILSMOKE_PATH)/package/directfb-mips
DIRECTFB_MIPS_SITE_METHOD = local
DIRECTFB_MIPS_DEPENDENCIES = directfb

# This is a meta-package — no source, no build, just a dependency.
# The real installation happens in directfb.mk (Buildroot's stock package).

define DIRECTFB_MIPS_BUILD_CMDS
    @echo "[lilsmoke] directfb-mips: meta-package, nothing to build"
endef

define DIRECTFB_MIPS_INSTALL_TARGET_CMDS
    @echo "[lilsmoke] directfb-mips: /etc/directfbrc installed by post-build.sh"
endef

$(eval $(generic-package))
