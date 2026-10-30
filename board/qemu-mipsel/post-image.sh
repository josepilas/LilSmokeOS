#!/bin/sh
#
# LilSmokeOS — post-image hook
#
# Called by Buildroot after the rootfs image is built.
# Args: $1 = BINARIES_DIR, $2 = BOARD_DIR
#
# Responsibilities:
#   1. Generate a FIT image (kernel + initramfs + dtb) for QEMU
#   2. Generate a simple flashable .img layout for real HW (stubbed in J0)
#   3. Write a version manifest

set -eu

BINARIES_DIR="$1"
BOARD_DIR="$2"

echo "[lilsmoke] post-image: BINARIES_DIR=$BINARIES_DIR BOARD_DIR=$BOARD_DIR"

# --- Version manifest ---
cat > "$BINARIES_DIR/lilsmoke-version.txt" <<EOF
LilSmokeOS 0.1.0-j0
target=$(basename "$BOARD_DIR")
build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
kernel=$(ls "$BINARIES_DIR"/vmlinux* 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo none)
rootfs=$(ls "$BINARIES_DIR"/rootfs.squashfs 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo none)
EOF

# --- FIT image for QEMU (J0 just uses vmlinux + initramfs directly) ---
# For real hardware (J4+) we'll use mkimage to build a proper FIT.
# For QEMU, scripts/run-qemu.sh loads vmlinux and feeds the cpio initramfs.

# --- Flashable .img for real HW (stub for J8) ---
if [ -f "$BINARIES_DIR/rootfs.squashfs" ]; then
    echo "[lilsmoke] rootfs.squashfs size: $(stat -c%s "$BINARIES_DIR/rootfs.squashfs") bytes"
fi

# --- Print summary ---
echo "[lilsmoke] post-image: artifacts in $BINARIES_DIR"
ls -lh "$BINARIES_DIR" | grep -E '(vmlinux|rootfs|initramfs|lilsmoke-version)' || true
