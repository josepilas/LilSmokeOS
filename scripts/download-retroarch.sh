#!/bin/bash
#
# LilSmokeOS — clone RetroArch
#
# Usage: ./scripts/download-retroarch.sh [target_dir]
#   target_dir defaults to ./dl/retroarch/
#
# Clones the RetroArch source at the version LilSmokeOS builds
# (matches RETROARCH_MIPS_VERSION in package/retroarch-mips/retroarch-mips.mk).
#
# Useful for:
#   - Offline builds
#   - Patching RetroArch locally (e.g., to add a custom video driver)
#   - Browsing the source for `rgui` menu code we don't ship but
#     reference in our config defaults
#
# After running this, you can point the build at your local copy by
# setting in your Buildroot config:
#   BR2_PACKAGE_RETROARCH_MIPS_SITE=/path/to/dl/retroarch
#   BR2_PACKAGE_RETROARCH_MIPS_SITE_METHOD=local
#
# Pin version: must match RETROARCH_MIPS_VERSION in
# package/retroarch-mips/retroarch-mips.mk (currently v1.19.1).

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET_DIR="${1:-$LILSMOKE_ROOT/dl/retroarch}"
RA_VERSION="v1.19.1"
RA_URL="https://github.com/libretro/RetroArch.git"

mkdir -p "$(dirname "$TARGET_DIR")"
cd "$(dirname "$TARGET_DIR")"

echo "[download-retroarch] target: $TARGET_DIR"
echo "[download-retroarch] version: $RA_VERSION"
echo ""

if [ -d "$TARGET_DIR/.git" ]; then
    echo "[download-retroarch] already cloned, fetching updates..."
    cd "$TARGET_DIR"
    git fetch --depth 1 origin "$RA_VERSION"
    git checkout "$RA_VERSION"
else
    echo -n "[download-retroarch] cloning from $RA_URL... "
    if git clone --depth 1 --branch "$RA_VERSION" "$RA_URL" "$TARGET_DIR" 2>/dev/null; then
        echo "OK"
    else
        echo "FAILED"
        echo "[download-retroarch] could not clone RetroArch $RA_VERSION" >&2
        exit 1
    fi
fi

echo ""
echo "[download-retroarch] RetroArch $RA_VERSION is at: $TARGET_DIR"
echo ""
echo "[download-retroarch] build configuration (matches our .mk):"
echo "  --enable-sdl2"
echo "  --enable-alsa"
echo "  --enable-freetype --enable-libpng --enable-zlib"
echo "  --disable-opengl --disable-opengles --disable-vulkan"
echo "  --disable-x11 --disable-wayland --disable-kms"
echo "  --disable-ffmpeg --disable-ssl --disable-systemd --disable-udev"
echo "  --enable-input=evdev"
echo ""
echo "[download-retroarch] to use this local copy in the build, add to defconfig:"
echo "  BR2_PACKAGE_RETROARCH_MIPS_SITE=\"$TARGET_DIR\""
echo "  BR2_PACKAGE_RETROARCH_MIPS_SITE_METHOD=local"
