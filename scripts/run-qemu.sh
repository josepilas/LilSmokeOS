#!/bin/bash
#
# LilSmokeOS — QEMU-mipsel launcher (malta board)
#
# Usage: ./scripts/run-qemu.sh [images_dir] [storage_dir]
#   images_dir defaults to ./build/buildroot/output/images
#   storage_dir defaults to ./storage
#
# Boots the LilSmokeOS QEMU-mipsel target. Requires:
#   - qemu-system-mipsel (apt install qemu-system-mips)
#   - A completed build (./scripts/build.sh qemu-mipsel)
#
# Memory: 256 MB (verified SF3000 memory capacity; GB350 remains unverified)
# Display: SDL window backed by an emulated Cirrus fbdev device
# Serial:  multiplexed to stdout
# Network: user-mode networking (10.0.2.x)

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGES_DIR="${1:-$LILSMOKE_ROOT/build/buildroot/output/images}"
STORAGE_DIR="${2:-$LILSMOKE_ROOT/storage}"
PERSIST_DIR="${LILSMOKE_PERSIST_DIR:-$LILSMOKE_ROOT/persist}"
AUDIO_BACKEND="${LILSMOKE_QEMU_AUDIO:-sdl}"
AUDIO_FILE="${LILSMOKE_QEMU_AUDIO_FILE:-$LILSMOKE_ROOT/lilsmoke-audio.wav}"
QEMU_BINARY="${LILSMOKE_QEMU_BINARY:-qemu-system-mipsel}"
QEMU_CPU="${LILSMOKE_QEMU_CPU:-74Kf}"
QEMU_RAM="${LILSMOKE_QEMU_RAM:-256}"
case "$QEMU_RAM" in
    128|256) ;;
    *) echo "error: LILSMOKE_QEMU_RAM must be 128 or 256" >&2; exit 1 ;;
esac

if [ ! -d "$IMAGES_DIR" ]; then
    echo "error: images dir not found: $IMAGES_DIR"
    echo "did you run ./scripts/build.sh qemu-mipsel ?"
    exit 1
fi

KERNEL="$IMAGES_DIR/vmlinux"
INITRAMFS="$IMAGES_DIR/rootfs.cpio.gz"

if [ ! -f "$KERNEL" ]; then
    echo "error: kernel not found: $KERNEL"
    exit 1
fi

if ! command -v "$QEMU_BINARY" >/dev/null 2>&1; then
    echo "error: qemu-system-mipsel not installed"
    echo "on Debian/Ubuntu: sudo apt install qemu-system-mips"
    exit 1
fi

echo "[lilsmoke] launching QEMU-mipsel"
echo "[lilsmoke]   kernel:     $KERNEL"
echo "[lilsmoke]   initramfs:  $INITRAMFS"
echo "[lilsmoke]   cpu:        $QEMU_CPU"
echo "[lilsmoke]   vcpus:      1"
echo "[lilsmoke]   memory:     $QEMU_RAM MiB"
echo "[lilsmoke]   storage:    $STORAGE_DIR"
echo "[lilsmoke]   persist:    $PERSIST_DIR"
echo "[lilsmoke]   audio:      AC97 -> $AUDIO_BACKEND"
echo "[lilsmoke]   serial:     stdout (Ctrl-A X to quit QEMU)"
echo ""

mkdir -p "$STORAGE_DIR" "$PERSIST_DIR"

case "$AUDIO_BACKEND" in
    wav)
        mkdir -p "$(dirname "$AUDIO_FILE")"
        AUDIO_ARGS=(-audiodev "wav,id=lilsmoke_audio,path=$AUDIO_FILE")
        ;;
    none|alsa|pa|pipewire|sdl)
        AUDIO_ARGS=(-audiodev "$AUDIO_BACKEND,id=lilsmoke_audio")
        ;;
    *)
        echo "error: unsupported LILSMOKE_QEMU_AUDIO backend: $AUDIO_BACKEND" >&2
        echo "supported backends: none, wav, alsa, pa, pipewire, sdl" >&2
        exit 1
        ;;
esac

# Malta board, 256 MB RAM, kernel + initramfs, serial to stdout
# -serial mon:stdio  : combine serial + monitor on stdio (Ctrl-A X to exit)
# -no-reboot          : kernel panic halts instead of rebooting (easier debug)
# -netdev user        : user-mode networking (DHCP from 10.0.2.2)
# -virtfs             : expose real PC folders as /storage and /persist
exec "$QEMU_BINARY" \
    -M malta \
    -cpu "$QEMU_CPU" \
    -accel tcg,thread=single \
    -smp 1 \
    -m "$QEMU_RAM" \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -append "console=ttyS0,115200n8 root=/dev/ram0 rdinit=/init video=cirrusfb:856x480M-24@60" \
    -serial mon:stdio \
    -no-reboot \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -vga cirrus \
    -device usb-kbd \
    "${AUDIO_ARGS[@]}" \
    -device AC97,audiodev=lilsmoke_audio \
    -virtfs "local,path=$STORAGE_DIR,mount_tag=storage,security_model=mapped-file" \
    -virtfs "local,path=$PERSIST_DIR,mount_tag=persist,security_model=mapped-file" \
    -display sdl,gl=off
