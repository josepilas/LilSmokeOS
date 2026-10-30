#!/bin/sh
#
# LilSmokeOS — post-image hook for VirtualBox x86_64
#
# Called by Buildroot after the rootfs image is built.
# Args: $1 = BINARIES_DIR, $2 = BOARD_DIR
#
# Responsibilities:
#   1. Generate a bootable ISO using genisoimage + ISOLINUX
#      - Kernel: bzImage
#      - Initramfs: rootfs.cpio.gz
#      - Bootloader: isolinux.bin + ldlinux.c32 (from host's syslinux)
#   2. Write a version manifest
#
# Output: $BINARIES_DIR/lilsmoke.iso
#
# Prerequisites on the build host:
#   - genisoimage (Debian: genisoimage, Fedora: cdrtools)
#   - isolinux.bin (Debian: isolinux package, file at /usr/lib/ISOLINUX/)
#   - ldlinux.c32 (Debian: syslinux-common, file at
#                  /usr/lib/syslinux/modules/bios/ldlinux.c32)
#
# See docs/vbox-guide.md §2 for installation instructions.

set -eu

BINARIES_DIR="$1"
BOARD_DIR="$2"

echo "[lilsmoke] post-image (vbox-x86_64): BINARIES_DIR=$BINARIES_DIR BOARD_DIR=$BOARD_DIR"

# --- Version manifest ---
cat > "$BINARIES_DIR/lilsmoke-version.txt" <<EOF
LilSmokeOS 0.7.0-j9
target=$(basename "$BOARD_DIR")
build_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
kernel=$(ls "$BINARIES_DIR"/bzImage 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo none)
rootfs=$(ls "$BINARIES_DIR"/rootfs.squashfs 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo none)
initramfs=$(ls "$BINARIES_DIR"/rootfs.cpio.gz 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo none)
EOF

# --- Locate the kernel + initramfs ---
KERNEL="$BINARIES_DIR/bzImage"
INITRAMFS="$BINARIES_DIR/rootfs.cpio.gz"

if [ ! -f "$KERNEL" ]; then
    echo "[lilsmoke] post-image: ERROR — kernel not found: $KERNEL" >&2
    echo "[lilsmoke] post-image: did you set BR2_LINUX_KERNEL_IMAGE_TARGET_NAME=\"bzImage\" in the defconfig?" >&2
    exit 1
fi
if [ ! -f "$INITRAMFS" ]; then
    echo "[lilsmoke] post-image: ERROR — initramfs not found: $INITRAMFS" >&2
    echo "[lilsmoke] post-image: did you set BR2_TARGET_ROOTFS_CPIO=y + BR2_TARGET_ROOTFS_CPIO_GZIP=y?" >&2
    exit 1
fi

# --- Locate ISOLINUX bootloader files on the host ---
# Debian / Ubuntu:
#   /usr/lib/ISOLINUX/isolinux.bin
#   /usr/lib/syslinux/modules/bios/ldlinux.c32
# Fedora / RHEL:
#   /usr/share/syslinux/isolinux.bin
#   /usr/share/syslinux/ldlinux.c32
ISOLINUX_BIN=""
LDLINUX_C32=""
for d in \
    /usr/lib/ISOLINUX \
    /usr/share/syslinux \
    /usr/lib/syslinux \
    /usr/lib/syslinux/modules/bios; do
    [ -f "$d/isolinux.bin" ] && ISOLINUX_BIN="$d/isolinux.bin"
    [ -f "$d/ldlinux.c32" ]  && LDLINUX_C32="$d/ldlinux.c32"
done

if [ -z "$ISOLINUX_BIN" ]; then
    echo "[lilsmoke] post-image: ERROR — isolinux.bin not found on host" >&2
    echo "[lilsmoke] post-image: install the 'isolinux' package (Debian/Ubuntu) or 'syslinux' (Fedora)" >&2
    exit 1
fi
if [ -z "$LDLINUX_C32" ]; then
    echo "[lilsmoke] post-image: ERROR — ldlinux.c32 not found on host" >&2
    echo "[lilsmoke] post-image: install the 'syslinux-common' package (Debian/Ubuntu) or 'syslinux' (Fedora)" >&2
    exit 1
fi

echo "[lilsmoke] post-image: isolinux.bin = $ISOLINUX_BIN"
echo "[lilsmoke] post-image: ldlinux.c32  = $LDLINUX_C32"

# --- Build the ISO staging tree ---
ISO_DIR="$BINARIES_DIR/iso-staging"
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/isolinux" "$ISO_DIR/boot"

# Copy the kernel and initramfs into /boot/ on the ISO
cp "$KERNEL"     "$ISO_DIR/boot/bzImage"
cp "$INITRAMFS"  "$ISO_DIR/boot/rootfs.cpio.gz"

# Copy the ISOLINUX files into /isolinux/ on the ISO
cp "$ISOLINUX_BIN" "$ISO_DIR/isolinux/isolinux.bin"
cp "$LDLINUX_C32"  "$ISO_DIR/isolinux/ldlinux.c32"
cp "$BOARD_DIR/isolinux.cfg" "$ISO_DIR/isolinux/isolinux.cfg"

# Also copy the squashfs into /boot/ if it was built (so the ISO can
# optionally boot from squashfs instead of initramfs)
if [ -f "$BINARIES_DIR/rootfs.squashfs" ]; then
    cp "$BINARIES_DIR/rootfs.squashfs" "$ISO_DIR/boot/rootfs.squashfs"
    echo "[lilsmoke] post-image: included rootfs.squashfs ($(stat -c%s "$BINARIES_DIR/rootfs.squashfs") bytes)"
fi

# --- Generate the ISO ---
# genisoimage options:
#   -o output file
#   -b boot image (relative to ISO root)
#   -c boot catalog (where the El Torito boot catalog is written)
#   -no-emul-boot    — don't pretend to be a floppy/hard disk
#   -boot-load-size  — number of 512-byte sectors to load
#   -boot-info-table — patches the boot image with partition info
#   -J               — Joliet extensions (for Windows compat)
#   -R               — Rock Ridge extensions (for Linux perm/long names)
ISO_OUT="$BINARIES_DIR/lilsmoke.iso"

echo "[lilsmoke] post-image: generating ISO..."
genisoimage \
    -o "$ISO_OUT" \
    -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -J -R \
    "$ISO_DIR"

# Clean up the staging dir
rm -rf "$ISO_DIR"

echo "[lilsmoke] post-image: ISO written to $ISO_OUT"
echo "[lilsmoke] post-image: ISO size: $(stat -c%s "$ISO_OUT") bytes"

# --- Print summary ---
echo "[lilsmoke] post-image (vbox-x86_64): artifacts in $BINARIES_DIR"
ls -lh "$BINARIES_DIR" | grep -E '(bzImage|rootfs|lilsmoke.iso|lilsmoke-version)' || true

cat <<EOF

[lilsmoke] post-image: to boot in VirtualBox:
  VBoxManage createvm --name LilSmokeOS --ostype Linux26_64 --register
  VBoxManage modifyvm LilSmokeOS --memory 256 --vram 16 --graphicscontroller vmsvga --nic1 nat --audio none --boot1 dvd
  VBoxManage storagectl LilSmokeOS --name IDE --add ide --controller PIIX4
  VBoxManage storageattach LilSmokeOS --storagectl IDE --port 1 --device 0 --type dvddrive --medium $ISO_OUT
  VBoxManage startvm LilSmokeOS

Or use the wrapper script:
  $(cd "$(dirname "$0")/../.." && pwd)/scripts/run-vbox.sh

EOF
