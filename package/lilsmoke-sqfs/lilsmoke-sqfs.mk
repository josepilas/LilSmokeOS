# Exact upstream v1.2.0 commit; no mutable branch or unavailable release asset.
LILSMOKE_SQFS_VERSION = f2a3ad56e40c9711b23371238f9fa07dd24245f1
LILSMOKE_SQFS_SITE = https://github.com/AgentD/squashfs-tools-ng.git
LILSMOKE_SQFS_SITE_METHOD = git
LILSMOKE_SQFS_AUTORECONF = YES
LILSMOKE_SQFS_LICENSE = LGPL-3.0+
LILSMOKE_SQFS_LICENSE_FILES = licenses/LGPLv3.txt licenses/GPLv3.txt
LILSMOKE_SQFS_INSTALL_STAGING = YES
LILSMOKE_SQFS_DEPENDENCIES = zlib xz
LILSMOKE_SQFS_CONF_OPTS = --without-tools --without-pthread --with-gzip --with-xz \
	--without-lzo --without-lz4 --without-zstd --without-bzip2 --without-selinux
LILSMOKE_SQFS_CONF_ENV = CFLAGS="$(TARGET_CFLAGS) -include $(LILSMOKE_SQFS_PKGDIR)/bounded-alloc.h"
$(eval $(autotools-package))
