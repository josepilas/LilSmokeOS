#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
SDK=/root/lilsmoke-os/build/buildroot/output
WORK=/root/lilsmoke-public-python
export PATH="$SDK/host/bin:/usr/local/bin:/usr/bin:/bin"
mkdir -p "$WORK/source" "$WORK/build" "$WORK/dest"
if [ ! -f "$WORK/source/configure" ]; then
    tar -xJf /root/lilsmoke-os/build/buildroot/dl/python3/Python-3.11.8.tar.xz \
        -C "$WORK/source" --strip-components=1
fi
cd "$WORK/build"
if [ ! -f Makefile ]; then
    CC=mipsel-buildroot-linux-musl-gcc AR=mipsel-buildroot-linux-musl-ar \
    RANLIB=mipsel-buildroot-linux-musl-ranlib \
    CFLAGS='-Os -mips32r2 -mtune=74kc' \
    ac_cv_file__dev_ptmx=yes ac_cv_file__dev_ptc=no \
    ac_cv_working_tzset=yes ac_cv_little_endian_double=yes \
    "$WORK/source/configure" --host=mipsel-buildroot-linux-musl \
        --build=x86_64-linux-gnu --prefix=/usr \
        --with-build-python="$SDK/host/bin/python3" \
        --without-ensurepip --disable-test-modules --disable-ipv6
fi
make -j2
make -j1 DESTDIR="$WORK/dest" install
"$SDK/host/bin/mipsel-buildroot-linux-musl-strip" "$WORK/dest/usr/bin/python3.11"
printf '%s\n' 'Native musl Python built; target execution still must pass.'
