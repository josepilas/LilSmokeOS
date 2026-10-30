#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work=${LILSMOKE_QEMU_BUILD_DIR:-/root/lilsmoke-performance/qemu-profile}
version=8.2.2
archive_sha=847346c1b82c1a54b2c38f6edbd85549edeb17430b7d4d3da12620e2962bc4f3
mkdir -p "$work"
archive="$work/qemu-$version.tar.xz"
if [ ! -f "$archive" ]; then
    curl --fail --location --retry 3 "https://download.qemu.org/qemu-$version.tar.xz" -o "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' "$archive_sha" "$archive" | sha256sum -c -
sha256sum "$archive" >"$work/source.sha256"
source_dir="$work/qemu-$version"
if [ ! -d "$source_dir" ]; then tar -xJf "$archive" -C "$work"; fi
if ! grep -q '74Kf-lilsmoke' "$source_dir/target/mips/cpu-defs.c.inc"; then
    patch --batch --forward -d "$source_dir" -p1 <"$root/tools/qemu/0001-lilsmoke-74kf-profile.patch"
fi
if ! sed -n '/name = "74Kf-lilsmoke"/,/CP0_LLAddr_rw/p' "$source_dir/target/mips/cpu-defs.c.inc" | grep -q CP0_Config7; then
    patch --batch --forward -d "$source_dir" -p1 <"$root/tools/qemu/0002-lilsmoke-wait-interrupt.patch"
fi
mkdir -p "$work/build"
cd "$work/build"
if [ ! -f build.ninja ]; then
    "$source_dir/configure" --target-list=mipsel-softmmu --disable-docs \
        --disable-werror --disable-gtk --disable-opengl --disable-vnc \
        --disable-spice --disable-curses --enable-sdl --enable-virtfs \
        --enable-slirp --disable-download
fi
ninja -j2 qemu-system-mipsel
./qemu-system-mipsel -cpu help | grep '74Kf-lilsmoke'
echo "LILSMOKE_QEMU_PROFILE_BUILT=$work/build/qemu-system-mipsel"
