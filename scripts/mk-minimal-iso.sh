#!/bin/bash
#
# LilSmokeOS — build a minimal bootable ISO without Buildroot
#
# Usage:
#   ./scripts/mk-minimal-iso.sh --kernel <bzImage> --initramfs <rootfs.cpio.gz>
#                               --output <lilsmoke.iso>
#                               [--isolinux-cfg <path>]
#
# This script produces the same ISO that
# board/vbox-x86_64/post-image.sh produces, but it doesn't require a
# full Buildroot build. Useful for:
#   - Quickly testing a kernel panic / init issue with a custom kernel
#   - Booting a foreign distro's rootfs under LilSmokeOS's bootloader
#     for debugging
#   - CI smoke tests that just need a bootable ISO without paying for
#     a 30-min Buildroot run
#
# You must supply:
#   --kernel     a Linux bzImage (any version, any arch — but for
#                VBox it must be x86_64)
#   --initramfs  a gzip'd cpio initramfs
#   --output     the path to write the ISO
#
# Optional:
#   --isolinux-cfg  path to a custom isolinux.cfg (defaults to
#                   board/vbox-x86_64/isolinux.cfg)
#
# Prerequisites on the host (same as post-image.sh):
#   - genisoimage (Debian: genisoimage, Fedora: cdrtools)
#   - isolinux.bin (Debian: isolinux package)
#   - ldlinux.c32  (Debian: syslinux-common)

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --- Defaults ---
KERNEL=""
INITRAMFS=""
OUTPUT=""
ISOLINUX_CFG="$LILSMOKE_ROOT/board/vbox-x86_64/isolinux.cfg"

# --- Parse args ---
while [ $# -gt 0 ]; do
    case "$1" in
        --kernel)       KERNEL="$2"; shift 2 ;;
        --initramfs)    INITRAMFS="$2"; shift 2 ;;
        --output)       OUTPUT="$2"; shift 2 ;;
        --isolinux-cfg) ISOLINUX_CFG="$2"; shift 2 ;;
        --help|-h)
            head -30 "$0" | tail -27
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# --- Validate ---
[ -z "$KERNEL" ]     && { echo "error: --kernel is required" >&2; exit 1; }
[ -z "$INITRAMFS" ]  && { echo "error: --initramfs is required" >&2; exit 1; }
[ -z "$OUTPUT" ]     && { echo "error: --output is required" >&2; exit 1; }
[ ! -f "$KERNEL" ]     && { echo "error: kernel not found: $KERNEL" >&2; exit 1; }
[ ! -f "$INITRAMFS" ]  && { echo "error: initramfs not found: $INITRAMFS" >&2; exit 1; }
[ ! -f "$ISOLINUX_CFG" ] && { echo "error: isolinux.cfg not found: $ISOLINUX_CFG" >&2; exit 1; }

# --- Locate ISOLINUX bootloader files on the host ---
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
    echo "error: isolinux.bin not found" >&2
    echo "  install the 'isolinux' package (Debian/Ubuntu) or 'syslinux' (Fedora)" >&2
    exit 1
fi
if [ -z "$LDLINUX_C32" ]; then
    echo "error: ldlinux.c32 not found" >&2
    echo "  install the 'syslinux-common' package (Debian/Ubuntu) or 'syslinux' (Fedora)" >&2
    exit 1
fi

if ! command -v genisoimage >/dev/null 2>&1; then
    echo "error: genisoimage not found" >&2
    echo "  install 'genisoimage' (Debian/Ubuntu) or 'cdrtools' (Fedora/Arch)" >&2
    exit 1
fi

echo "[mk-minimal-iso] kernel:       $KERNEL ($(stat -c%s "$KERNEL") bytes)"
echo "[mk-minimal-iso] initramfs:    $INITRAMFS ($(stat -c%s "$INITRAMFS") bytes)"
echo "[mk-minimal-iso] isolinux.cfg: $ISOLINUX_CFG"
echo "[mk-minimal-iso] output:       $OUTPUT"
echo "[mk-minimal-iso] isolinux.bin: $ISOLINUX_BIN"
echo "[mk-minimal-iso] ldlinux.c32:  $LDLINUX_C32"
echo ""

# --- Build the ISO staging tree ---
STAGING=$(mktemp -d)
trap "rm -rf $STAGING" EXIT

mkdir -p "$STAGING/isolinux" "$STAGING/boot"

cp "$KERNEL"     "$STAGING/boot/bzImage"
cp "$INITRAMFS"  "$STAGING/boot/rootfs.cpio.gz"
cp "$ISOLINUX_BIN" "$STAGING/isolinux/isolinux.bin"
cp "$LDLINUX_C32"  "$STAGING/isolinux/ldlinux.c32"
cp "$ISOLINUX_CFG" "$STAGING/isolinux/isolinux.cfg"

# --- Generate the ISO ---
echo "[mk-minimal-iso] generating ISO..."
genisoimage \
    -o "$OUTPUT" \
    -b isolinux/isolinux.bin \
    -c isolinux/boot.cat \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -J -R \
    "$STAGING"

echo "[mk-minimal-iso] ISO written to $OUTPUT ($(stat -c%s "$OUTPUT") bytes)"
echo ""
echo "[mk-minimal-iso] to boot in VirtualBox:"
echo "  ./scripts/run-vbox.sh $OUTPUT"
echo ""
echo "[mk-minimal-iso] to mount and inspect (without booting):"
echo "  mkdir -p /mnt/iso && sudo mount -o loop $OUTPUT /mnt/iso"
