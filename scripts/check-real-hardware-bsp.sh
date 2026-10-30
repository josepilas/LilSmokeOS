#!/bin/sh
# Refuse to label a real-device image flashable before every board-critical
# HC16xx capability has been demonstrated on the target revision.

set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
TARGET="${1:-}"

case "$TARGET" in
    sf3000|gb350) ;;
    *)
        echo "usage: check-real-hardware-bsp.sh sf3000|gb350" >&2
        exit 2
        ;;
esac

STATUS="$ROOT/board/$TARGET/bsp-status.conf"
[ -r "$STATUS" ] || {
    echo "[lilsmoke] ERROR: missing BSP status manifest: $STATUS" >&2
    exit 1
}

status_get() {
    awk -F= -v wanted="$1" '$1 == wanted { sub(/^[^=]*=/, ""); print; exit }' "$STATUS"
}

missing=""
for capability in CPU INPUT STORAGE DISPLAY AUDIO POWER THERMAL BOOT; do
    value="$(status_get "${capability}_VALIDATED")"
    if [ "$value" != yes ]; then
        missing="$missing\n  - ${capability}_VALIDATED=${value:-missing}"
    fi
done

if [ -n "$missing" ]; then
    echo "[lilsmoke] ERROR: the $TARGET userspace profile is available, but its real-hardware BSP is incomplete." >&2
    printf '[lilsmoke] Unresolved capabilities:%b\n' "$missing" >&2
    echo "[lilsmoke] Refusing to create or advertise a flashable image." >&2
    echo "[lilsmoke] Status source: $STATUS" >&2
    echo "[lilsmoke] QEMU userspace testing: scripts/build.sh qemu-mipsel" >&2
    exit 1
fi

repo="$(status_get KERNEL_REPO)"
commit="$(status_get KERNEL_COMMIT)"
case "$repo:$commit" in
    http*:[0-9a-f][0-9a-f]*) ;;
    *)
        echo "[lilsmoke] ERROR: validated BSP needs a pinned KERNEL_REPO and KERNEL_COMMIT" >&2
        exit 1
        ;;
esac

echo "[lilsmoke] $TARGET BSP capabilities are marked validated at kernel $commit"
