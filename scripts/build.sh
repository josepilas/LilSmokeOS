#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
# Configure/build a pinned Buildroot target; never flash a physical device.
set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDROOT_VERSION="2024.02.3"
BUILDROOT_DIR="${LILSMOKE_BUILDROOT_DIR:-$LILSMOKE_ROOT/build/buildroot}"
case "$BUILDROOT_DIR" in /*) ;; *) BUILDROOT_DIR="$PWD/$BUILDROOT_DIR" ;; esac
OUTPUT_DIR="${LILSMOKE_OUTPUT_DIR:-$BUILDROOT_DIR/output}"
case "$OUTPUT_DIR" in /*) ;; *) OUTPUT_DIR="$PWD/$OUTPUT_DIR" ;; esac
TARGET="${1:-qemu-mipsel}"
CONFIGURE_ONLY=0

list_targets() {
    local file name
    for file in "$LILSMOKE_ROOT"/configs/lilsmoke_*_defconfig; do
        [ -f "$file" ] || continue
        name="${file##*/lilsmoke_}"
        name="${name%_defconfig}"
        printf '  %s\n' "${name/_/-}"
    done
}

usage() {
    echo "Usage: $0 [target] [--configure-only]"
    echo "       $0 --list-targets | menuconfig | clean"
    echo "Default target: qemu-mipsel"
    echo "Targets:"
    list_targets
    echo "Optional: LILSMOKE_BUILDROOT_DIR, LILSMOKE_OUTPUT_DIR, LILSMOKE_JOBS"
}

if [ "$#" -gt 2 ]; then usage >&2; exit 2; fi
if [ "$#" -eq 2 ]; then
    if [ "$2" != --configure-only ]; then usage >&2; exit 2; fi
    CONFIGURE_ONLY=1
fi
case "$TARGET" in
    --help|-h) usage; exit 0 ;;
    --list-targets) list_targets; exit 0 ;;
    clean|menuconfig)
        if [ "$CONFIGURE_ONLY" -eq 1 ]; then usage >&2; exit 2; fi
        ;;
    *)
        case "$TARGET" in
            *[!a-zA-Z0-9_-]*) echo "[lilsmoke] invalid target: $TARGET" >&2; exit 2 ;;
        esac
        DEFCONFIG="lilsmoke_${TARGET//-/_}_defconfig"
        if [ ! -f "$LILSMOKE_ROOT/configs/$DEFCONFIG" ]; then
            echo "[lilsmoke] unknown or missing target: $TARGET" >&2
            echo "[lilsmoke] available targets:" >&2
            list_targets >&2
            exit 1
        fi
        ;;
esac

# Loading a userspace profile is safe; building a hardware image remains gated.
if [ "$CONFIGURE_ONLY" -eq 0 ]; then
    case "$TARGET" in
        sf3000|gb350)
            if [ "${LILSMOKE_ALLOW_UNVERIFIED_BSP:-0}" != 1 ]; then
                sh "$LILSMOKE_ROOT/scripts/check-real-hardware-bsp.sh" "$TARGET"
            else
                echo "[lilsmoke] WARNING: bypassing the real-hardware BSP safety gate"
            fi
            ;;
    esac
fi

if [ ! -d "$BUILDROOT_DIR" ]; then
    echo "[lilsmoke] cloning Buildroot $BUILDROOT_VERSION..."
    mkdir -p "$(dirname "$BUILDROOT_DIR")"
    git clone --branch "$BUILDROOT_VERSION" --depth 1 \
        https://github.com/buildroot/buildroot.git "$BUILDROOT_DIR"
fi
if [ ! -f "$BUILDROOT_DIR/Makefile" ]; then
    echo "[lilsmoke] Buildroot Makefile missing: $BUILDROOT_DIR" >&2
    exit 1
fi
MAKE_ARGS=(BR2_EXTERNAL="$LILSMOKE_ROOT" O="$OUTPUT_DIR")

case "$TARGET" in
    clean|menuconfig)
        make -C "$BUILDROOT_DIR" "${MAKE_ARGS[@]}" "$TARGET"
        exit 0
        ;;
esac

echo "[lilsmoke] loading defconfig: $DEFCONFIG"
make -C "$BUILDROOT_DIR" "${MAKE_ARGS[@]}" "$DEFCONFIG"
if [ "$CONFIGURE_ONLY" -eq 1 ]; then
    echo "[lilsmoke] configured only: $OUTPUT_DIR/.config (no packages/images built)"
    exit 0
fi

JOBS="${LILSMOKE_JOBS:-$(nproc)}"
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "[lilsmoke] LILSMOKE_JOBS must be a positive integer" >&2
    exit 2
fi
echo "[lilsmoke] building target $TARGET with $JOBS jobs..."
make -C "$BUILDROOT_DIR" "${MAKE_ARGS[@]}" -j"$JOBS"

echo "[lilsmoke] build complete; artifacts: $OUTPUT_DIR/images/"
ls -lh "$OUTPUT_DIR/images/" 2>/dev/null | head -20 || true
