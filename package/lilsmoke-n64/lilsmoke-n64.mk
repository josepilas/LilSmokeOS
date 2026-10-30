LILSMOKE_N64_VERSION = 0bd516ee793bb87b57e9eef19993b37ac0098d74
LILSMOKE_N64_SITE = https://github.com/libretro/parallel-n64.git
LILSMOKE_N64_SITE_METHOD = git
LILSMOKE_N64_LICENSE = GPL-2.0+, BSD-3-Clause, MIT
LILSMOKE_N64_LICENSE_FILES = mupen64plus-rsp-cxd4/COPYING mupen64plus-core/LICENSES

# This is a shared libretro core: -mno-shared is NOT valid for this artifact.
# Do not inherit the build host's architecture/dynarec or unsafe -Ofast.
define LILSMOKE_N64_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -j$(PARALLEL_JOBS) \
        CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" \
        platform=unix ARCH=mips WITH_DYNAREC= NOSSE=1 \
        HAVE_OPENGL=0 HAVE_PARALLEL=0 HAVE_PARALLEL_RSP=0 HAVE_THR_AL=1 \
        CPUOPTS="-O3 -fno-strict-aliasing -DNDEBUG -DLILSMOKE_N64_NO_DD -DLILSMOKE_N64_LOW_RAM" \
        CPUFLAGS="$(TARGET_CFLAGS) -mips32r2 -mtune=74kc -mhard-float -mfp32" \
        GIT_VERSION=" $(LILSMOKE_N64_VERSION)"
endef

define LILSMOKE_N64_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0644 $(@D)/parallel_n64_libretro.so \
        $(TARGET_DIR)/usr/lib/libretro/parallel_n64_libretro.so
    $(INSTALL) -D -m 0644 $(LILSMOKE_N64_PKGDIR)/parallel_n64_libretro.info \
        $(TARGET_DIR)/usr/share/libretro/info/parallel_n64_libretro.info
endef

$(eval $(generic-package))
