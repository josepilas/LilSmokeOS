LILSMOKE_FFMPEG_VERSION = 6.1.6
LILSMOKE_FFMPEG_SOURCE = ffmpeg-$(LILSMOKE_FFMPEG_VERSION).tar.xz
LILSMOKE_FFMPEG_SITE = https://ffmpeg.org/releases
LILSMOKE_FFMPEG_LICENSE = LGPL-2.1+, libjpeg license
LILSMOKE_FFMPEG_LICENSE_FILES = LICENSE.md COPYING.LGPLv2.1
LILSMOKE_FFMPEG_INSTALL_STAGING = YES
LILSMOKE_FFMPEG_DEPENDENCIES = host-pkgconf zlib
LILSMOKE_FFMPEG_CONF_OPTS = --prefix=/usr --enable-cross-compile \
    --cross-prefix=$(TARGET_CROSS) --cc="$(TARGET_CC)" --cxx="$(TARGET_CXX)" \
    --ar="$(TARGET_AR)" --nm="$(TARGET_NM)" --strip="$(TARGET_STRIP)" \
    --sysroot=$(STAGING_DIR) --target-os=linux --arch=$(BR2_ARCH) \
    --enable-shared --disable-static --enable-pic --enable-small \
    --disable-autodetect --enable-ffmpeg --enable-ffprobe --disable-ffplay --disable-doc --disable-debug \
    --disable-avdevice --enable-avfilter \
    --disable-postproc --disable-network --disable-protocols --enable-protocol=file --enable-protocol=pipe \
    --disable-hwaccels --disable-vaapi --disable-vdpau --disable-vulkan \
    --disable-opencl --disable-opengl --disable-cuda --disable-cuvid --disable-nvenc \
    --disable-sdl2 --disable-gpl --disable-nonfree --enable-zlib \
    --extra-cflags="$(TARGET_CFLAGS) -O2 -fPIC" --extra-ldflags="$(TARGET_LDFLAGS)"
ifeq ($(BR2_mipsel),y)
LILSMOKE_FFMPEG_CONF_OPTS += --arch=mips --cpu=mips32r2 --disable-msa \
    --disable-mipsdsp --disable-mipsdspr2
endif
define LILSMOKE_FFMPEG_CONFIGURE_CMDS
    cd $(@D) && $(TARGET_CONFIGURE_OPTS) ./configure $(LILSMOKE_FFMPEG_CONF_OPTS)
endef
define LILSMOKE_FFMPEG_BUILD_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef
define LILSMOKE_FFMPEG_INSTALL_STAGING_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(STAGING_DIR) install
endef
define LILSMOKE_FFMPEG_INSTALL_TARGET_CMDS
    $(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install-libs install-progs
    $(INSTALL) -D -m 0644 $(@D)/COPYING.LGPLv2.1 \
        $(TARGET_DIR)/usr/share/licenses/lilsmoke-ffmpeg/COPYING.LGPLv2.1
endef
$(eval $(generic-package))
