#!/bin/bash
#
# LilSmokeOS — top-level build wrapper
#
# Usage:
#   ./scripts/build.sh                # build for current defconfig (qemu-mipsel)
#   ./scripts/build.sh qemu-mipsel    # build QEMU target
#   ./scripts/build.sh sf3000         # build SF3000 target (J4+)
#   ./scripts/build.sh gb350          # build GB350 target (J4+)
#   ./scripts/build.sh menuconfig     # open menuconfig
#   ./scripts/build.sh clean          # clean build
#
# This script:
#   1. Clones Buildroot to ./build/buildroot/ if missing (pinned to 2024.02.x)
#   2. Applies LilSmokeOS as external tree
#   3. Loads the right defconfig
#   4. Runs make

set -euo pipefail

case "$(uname -s 2>/dev/null || true)" in
    MSYS*|MINGW*|CYGWIN*)
        # Kernel sources contain dangling symlinks while extracting; MSYS'
        # default native symlink mode fails on those before the target appears.
        export MSYS="${MSYS:-winsymlinks:lnk}"
        ;;
esac

# --- Paths ---
LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDROOT_DIR="$LILSMOKE_ROOT/build/buildroot"
BUILDROOT_VERSION="2024.02.3"
TARGET="${1:-qemu-mipsel}"

cd "$LILSMOKE_ROOT"

# --- Clone Buildroot if needed ---
if [ ! -d "$BUILDROOT_DIR" ]; then
    echo "[lilsmoke] cloning Buildroot $BUILDROOT_VERSION..."
    mkdir -p "$(dirname "$BUILDROOT_DIR")"
    git clone --branch "$BUILDROOT_VERSION" --depth 1 \
        https://github.com/buildroot/buildroot.git "$BUILDROOT_DIR"
fi

cd "$BUILDROOT_DIR"

# --- Handle non-build subcommands ---
case "$TARGET" in
    clean)
        echo "[lilsmoke] cleaning buildroot"
        make clean
        exit 0
        ;;
    menuconfig)
        make BR2_EXTERNAL="$LILSMOKE_ROOT" menuconfig
        exit 0
        ;;
esac

# --- Load defconfig ---
DEFCONFIG="lilsmoke_${TARGET//-/_}_defconfig"
if [ ! -f "$LILSMOKE_ROOT/configs/$DEFCONFIG" ]; then
    echo "[lilsmoke] unknown target: $TARGET"
    echo "[lilsmoke] available targets:"
    ls "$LILSMOKE_ROOT/configs/" | sed 's/lilsmoke_//;s/_defconfig//'
    exit 1
fi

echo "[lilsmoke] loading defconfig: $DEFCONFIG"

case "$TARGET" in
    sf3000|gb350)
        if [ "${LILSMOKE_ALLOW_UNVERIFIED_BSP:-0}" != "1" ]; then
            sh "$LILSMOKE_ROOT/scripts/check-real-hardware-bsp.sh" "$TARGET"
        else
            echo "[lilsmoke] WARNING: bypassing the real-hardware BSP safety gate"
        fi
        ;;
esac

make BR2_EXTERNAL="$LILSMOKE_ROOT" "$DEFCONFIG"

# --- Build ---
echo "[lilsmoke] building (this takes ~45 min on a fast machine)..."
JOBS="${LILSMOKE_JOBS:-$(nproc)}"
make BR2_EXTERNAL="$LILSMOKE_ROOT" -j"$JOBS"

# --- Done ---
echo ""
echo "[lilsmoke] build complete!"
echo "[lilsmoke] artifacts in: $BUILDROOT_DIR/output/images/"
ls -lh "$BUILDROOT_DIR/output/images/" 2>/dev/null | head -20
echo ""
echo "[lilsmoke] to run in QEMU:"
echo "  $LILSMOKE_ROOT/scripts/run-qemu.sh $BUILDROOT_DIR/output/images"
