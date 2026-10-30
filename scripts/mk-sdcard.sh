#!/bin/bash
# Build a two-partition STORAGE DEVELOPMENT image, not a production BSP.
# Usage: --output FILE --board BOARD --rootfs FILE --development
#        [--size MB] [--roms-fs exfat|vfat] [--rootfs-format fit|squashfs] [--kernel FILE]
# Only new regular image files are accepted. Never writes a physical device.
set -euo pipefail
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
OUTPUT='' BOARD='' ROOTFS='' DEVELOPMENT=no
SIZE_MB=640
ROMS_FS=exfat
ROOTFS_FORMAT=fit
KERNEL=''
OS_MB=512
LOOP_DEV='' TEMP=''
cleanup() {
    if [ -n "$TEMP" ]; then
        mountpoint -q "$TEMP/os" && umount "$TEMP/os" || true
        mountpoint -q "$TEMP/roms" && umount "$TEMP/roms" || true
    fi
    [ -z "$LOOP_DEV" ] || losetup -d "$LOOP_DEV"
    [ -z "$TEMP" ] || rmdir "$TEMP/os" "$TEMP/roms" "$TEMP" 2>/dev/null || true
}
trap cleanup EXIT
while [ "$#" -gt 0 ]; do
    case "$1" in
        --development) DEVELOPMENT=yes; shift ;;
        --output|--board|--rootfs|--size|--roms-fs|--rootfs-format|--kernel)
            [ "$#" -ge 2 ] || { echo "Missing value: $1" >&2; exit 64; }
            case "$1" in
                --output) OUTPUT=$2 ;; --board) BOARD=$2 ;; --rootfs) ROOTFS=$2 ;;
                --size) SIZE_MB=$2 ;; --roms-fs) ROMS_FS=$2 ;;
                --rootfs-format) ROOTFS_FORMAT=$2 ;; --kernel) KERNEL=$2 ;;
            esac
            shift 2 ;;
        --device|--uboot|--uboot-env) echo "Raw device/bootloader writes are unsupported: production BSP is unverified" >&2; exit 1 ;;
        --help|-h) head -5 "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 64 ;;
    esac
done
[ "$DEVELOPMENT" = yes ] || { echo "Production boot adapter is not validated. Use --development for storage QA only." >&2; exit 1; }
case "$BOARD" in sf3000|gb350|qemu-mipsel|hc16xx-common) ;; *) echo "Invalid board" >&2; exit 1 ;; esac
case "$ROMS_FS" in exfat|vfat) ;; *) exit 1 ;; esac
case "$ROOTFS_FORMAT" in fit|squashfs) ;; *) echo "Invalid rootfs format" >&2; exit 64 ;; esac
if [ "$ROOTFS_FORMAT" = squashfs ]; then
    [ -f "$ROOTFS" ] && [ "$(head -c 4 "$ROOTFS")" = hsqs ] || {
        echo "Expected a little-endian SquashFS image, not an initramfs/ISO" >&2; exit 1;
    }
fi
[ -z "$KERNEL" ] || [ -f "$KERNEL" ] || { echo "Kernel file missing" >&2; exit 1; }
case "$SIZE_MB" in ''|*[!0-9]*) exit 1 ;; esac
[ "${#SIZE_MB}" -le 7 ] && [ "$SIZE_MB" -ge 640 ] && [ "$SIZE_MB" -le 2000000 ] || exit 1
[ -f "$ROOTFS" ] && [ -n "$OUTPUT" ] && [ ! -e "$OUTPUT" ] && [ ! -L "$OUTPUT" ] || { echo "Need a payload and a NEW output image" >&2; exit 1; }
for tool in sfdisk losetup mkfs.vfat mkfs.exfat mke2fs mount umount truncate sha256sum python3; do
    command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
PAYLOAD_BYTES=$(stat -c%s "$ROOTFS")
[ -z "$KERNEL" ] || PAYLOAD_BYTES=$((PAYLOAD_BYTES+$(stat -c%s "$KERNEL")))
[ "$PAYLOAD_BYTES" -le 200000000 ] || { echo "Payload leaves insufficient first-boot backup space" >&2; exit 1; }
TEMP=$(mktemp -d)
mkdir "$TEMP/os" "$TEMP/roms"
(set -o noclobber; : > "$OUTPUT")
truncate -s "${SIZE_MB}M" "$OUTPUT"
P2_START=$((2048+OS_MB*2048))
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%s, type=c, bootable\nstart=%s, size=260096, type=%s\n' \
    "$((OS_MB*2048))" "$P2_START" "$([ "$ROMS_FS" = exfat ] && echo 7 || echo c)" | sfdisk "$OUTPUT"
LOOP_DEV=$(losetup --find --show --partscan "$OUTPUT")
mkfs.vfat -F 32 -n LILOS "${LOOP_DEV}p1"
if [ "$ROMS_FS" = exfat ]; then mkfs.exfat -L LILROMS "${LOOP_DEV}p2"; else mkfs.vfat -F 32 -n LILROMS "${LOOP_DEV}p2"; fi
mount "${LOOP_DEV}p1" "$TEMP/os"
mkdir -p "$TEMP/os/lilsmoke/slots/A" "$TEMP/os/lilsmoke/slots/B"
if [ "$ROOTFS_FORMAT" = squashfs ]; then
    cp "$ROOTFS" "$TEMP/os/lilsmoke/slots/A/rootfs.squashfs"
else
    cp "$ROOTFS" "$TEMP/os/lilsmoke/slots/A/lilsmoke.itb"
fi
if [ -n "$KERNEL" ]; then cp "$KERNEL" "$TEMP/os/lilsmoke/slots/A/vmlinux"; fi
(cd "$TEMP/os/lilsmoke/slots/A"; sha256sum ./* > SHA256SUMS)
truncate -s 64M "$TEMP/os/lilsmoke/persist.ext4"
mke2fs -q -t ext4 -F -m 0 -L LILPERSIST "$TEMP/os/lilsmoke/persist.ext4"
printf 'LayoutVersion=2\nBoard=%s\nROMStart=%s\nExpandOnFirstBoot=yes\nBootAdapter=unverified\nRootFSFormat=%s\nSystemPackage=main.syspck\n' \
    "$BOARD" "$P2_START" "$ROOTFS_FORMAT" > "$TEMP/os/lilsmoke/layout.conf"
printf 'Storage QA only. Not production-bootable firmware.\n' > "$TEMP/os/DEVELOPMENT.txt"
mount "${LOOP_DEV}p2" "$TEMP/roms"
mkdir -p "$TEMP/roms/roms" "$TEMP/roms/bios" "$TEMP/roms/themes" "$TEMP/roms/portsmoke/ports" "$TEMP/roms/portsmoke/engines" "$TEMP/roms/portsmoke/saves"
sync
umount "$TEMP/roms"
umount "$TEMP/os"
losetup -d "$LOOP_DEV"
LOOP_DEV=''
python3 "$SCRIPT_DIR/../tools/images/inspect_sdcard.py" --lilsmoke --sha256 "$OUTPUT" > "$OUTPUT.layout.json"
(cd "$(dirname -- "$OUTPUT")"; sha256sum "$(basename -- "$OUTPUT")") > "$OUTPUT.sha256"
echo "Created $OUTPUT: p1 LILOS FAT32 ${OS_MB}MiB; p2 LILROMS $ROMS_FS."
echo "LILROMS grows on first boot after imaging onto a larger disk."
