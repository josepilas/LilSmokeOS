#!/bin/bash
# Cross-compile and execute the SDL2_image WebP probe in the MIPS target VM.

set -Eeuo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <buildroot-images-dir> <storage-dir> <output-log>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGES_DIR="$(realpath "$1")"
STORAGE_DIR="$(realpath "$2")"
OUTPUT_LOG="$(realpath -m "$3")"
BUILDROOT_OUTPUT="$(dirname "$IMAGES_DIR")"
HOST_DIR="$BUILDROOT_OUTPUT/host"
CC="$HOST_DIR/bin/mipsel-buildroot-linux-musl-gcc"
KERNEL="$IMAGES_DIR/vmlinux"
INITRAMFS="$IMAGES_DIR/rootfs.cpio.gz"
WEBP_RELATIVE="media/snes/super-mario-world.webp"
RUN_DIR="$(mktemp -d /tmp/lilsmoke-webp-probe-XXXXXX)"
ROOTFS_DIR="$RUN_DIR/rootfs"
PROBE_INITRAMFS="$RUN_DIR/rootfs-webp-probe.cpio.gz"
SERIAL_LOG="$RUN_DIR/guest-serial.log"
HOST_LOG="$RUN_DIR/qemu-host.log"
PERSIST_DIR="$RUN_DIR/persist"
QEMU_PID=""

cleanup() {
    if [ -n "$QEMU_PID" ]; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rm -rf "$RUN_DIR"
}
trap cleanup EXIT INT TERM

for path in "$CC" "$KERNEL" "$INITRAMFS" \
            "$SCRIPT_DIR/webp_probe.c" "$STORAGE_DIR/$WEBP_RELATIVE"; do
    [ -e "$path" ] || { echo "missing required path: $path" >&2; exit 3; }
done
for tool in cpio file gzip qemu-system-mipsel realpath; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "missing required host tool: $tool" >&2
        exit 3
    }
done

mkdir -p "$ROOTFS_DIR" "$PERSIST_DIR" "$(dirname "$OUTPUT_LOG")"

"$CC" -O2 -o "$RUN_DIR/webp_probe" "$SCRIPT_DIR/webp_probe.c" \
    -lSDL2_image -lSDL2
file "$RUN_DIR/webp_probe" | grep -q 'ELF 32-bit LSB.*MIPS.*MIPS32 rel2' || {
    echo "probe is not a MIPS32r2 little-endian executable" >&2
    exit 4
}

gzip -dc "$INITRAMFS" | (cd "$ROOTFS_DIR" && cpio -idm --quiet)
install -D -m 0755 "$RUN_DIR/webp_probe" "$ROOTFS_DIR/usr/bin/webp_probe"
cat > "$ROOTFS_DIR/etc/init.d/S88webp-probe" <<'EOF'
#!/bin/sh
case "$1" in
    start)
        echo "[lilsmoke-webp-probe] starting target decoder check"
        /usr/bin/webp_probe /storage/media/snes/super-mario-world.webp
        status=$?
        echo "[lilsmoke-webp-probe] exit=$status"
        sync
        sleep 1
        poweroff -f
        ;;
esac
exit 0
EOF
chmod 0755 "$ROOTFS_DIR/etc/init.d/S88webp-probe"

(cd "$ROOTFS_DIR" && find . -print0 | \
    cpio --null -o --format=newc --owner=0:0 --quiet) | \
    gzip -9 > "$PROBE_INITRAMFS"

set +e
qemu-system-mipsel \
    -M malta \
    -cpu 74Kf \
    -accel tcg,thread=single \
    -smp 1 \
    -m 256 \
    -kernel "$KERNEL" \
    -initrd "$PROBE_INITRAMFS" \
    -append 'console=ttyS0,115200n8 root=/dev/ram0 rdinit=/init video=cirrusfb:856x480M-24@60' \
    -serial "file:$SERIAL_LOG" \
    -monitor none \
    -no-reboot \
    -vga cirrus \
    -nic user,model=pcnet \
    -virtfs "local,path=$STORAGE_DIR,mount_tag=storage,security_model=none" \
    -virtfs "local,path=$PERSIST_DIR,mount_tag=persist,security_model=none" \
    -display none \
    >"$HOST_LOG" 2>&1 &
QEMU_PID=$!
qemu_status=124
for _ in $(seq 1 240); do
    if grep -Fq '[lilsmoke-webp-probe] exit=0' "$SERIAL_LOG" 2>/dev/null; then
        qemu_status=0
        break
    fi
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        wait "$QEMU_PID"
        qemu_status=$?
        QEMU_PID=""
        break
    fi
    sleep 1
done
if [ -n "$QEMU_PID" ]; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
    QEMU_PID=""
fi
set -e

{
    echo "command_status=$qemu_status"
    echo "probe_binary=$(file "$RUN_DIR/webp_probe")"
    echo "--- guest serial ---"
    cat "$SERIAL_LOG"
    echo "--- qemu host ---"
    cat "$HOST_LOG"
} > "$OUTPUT_LOG"

grep -Fq 'WEBP_PROBE_OK width=320 height=240 format=SDL_PIXELFORMAT_RGB24' \
    "$SERIAL_LOG" || {
        echo "target WebP probe failed; see $OUTPUT_LOG" >&2
        exit 5
    }
grep -Fq '[lilsmoke-webp-probe] exit=0' "$SERIAL_LOG" || {
    echo "target WebP probe returned non-zero; see $OUTPUT_LOG" >&2
    exit 5
}

echo "TARGET_WEBP_PROBE_OK log=$OUTPUT_LOG"
