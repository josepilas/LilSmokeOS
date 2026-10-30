#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
REF=$(printenv OD_SQFS_REF || printf /root/lilsmoke-m8-bringup-2026-10-06/squashfs-ng-reference)
OUT=$(printenv OD_BUILD || printf /root/lilsmoke-opendingux-qa)
B=$(printenv OD_BUILDROOT_OUTPUT || printf /root/lilsmoke-os/build/buildroot/output)
MODE=$1
mkdir -p "$OUT"
if [ ! -f "$OUT/sqfs-source/configure" ]; then
    mkdir -p "$OUT/sqfs-source"
    git -C "$REF" archive v1.2.0 | tar -x -C "$OUT/sqfs-source"
    (cd "$OUT/sqfs-source" && patch -p1 < "$P/package/lilsmoke-sqfs/0001-bound-xz-decoder.patch")
    (cd "$OUT/sqfs-source" && autoreconf -fi)
fi
mkdir -p "$OUT/$MODE/sqfs" "$OUT/$MODE/runtime"
if [ "$MODE" = mips ]; then
    CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc"
    SYSROOT="$B/staging"
    export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    CROSS="--host=mipsel-buildroot-linux-musl"
    CF="-Os -march=mips32r2 -mtune=74kc -mabi=32"
else
    CC=$(printenv CC || printf cc)
    CROSS=""
    CF="-O2"
fi
if [ ! -f "$OUT/$MODE/sqfs/Makefile" ]; then
    (cd "$OUT/$MODE/sqfs" && CC="$CC" CFLAGS="$CF -include $P/package/lilsmoke-sqfs/bounded-alloc.h" \
        "$OUT/sqfs-source/configure" $CROSS --prefix=/usr --without-tools --without-pthread \
        --with-gzip --with-xz --without-lzo --without-lz4 --without-zstd --without-bzip2 --without-selinux)
fi
make -C "$OUT/$MODE/sqfs" -j2
make -C "$OUT/$MODE/sqfs" DESTDIR="$OUT/$MODE/sysroot" install
"$CC" -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -I"$P/package/lilsmoke-opendingux/vendor/libini" \
    $CF -fPIC -shared -Wl,-soname,libini.so.0 \
    "$P/package/lilsmoke-opendingux/vendor/libini/libini.c" -o "$OUT/$MODE/runtime/libini.so.0"
"$CC" -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOD_TEST_PATHS -DOD_TEST_NO_SYSTEM_DEPS -DOD_HOSTED_QA \
    -I"$P/package/lilsmoke-opendingux/vendor/libini" -I"$OUT/$MODE/sysroot/usr/include" \
    $CF -std=c99 -Wall -Wextra -Werror "$P"/package/lilsmoke-opendingux/src/*.c \
    -L"$OUT/$MODE/runtime" -l:libini.so.0 -L"$OUT/$MODE/sysroot/usr/lib" -lsquashfs -lsodium \
    -o "$OUT/$MODE/runtime/lilsmoke-opendingux"
echo "Isolated $MODE helper: $OUT/$MODE/runtime/lilsmoke-opendingux"
echo "Production build omits OD_TEST_PATHS, OD_TEST_NO_SYSTEM_DEPS and OD_HOSTED_QA."
