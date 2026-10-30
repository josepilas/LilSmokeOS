#!/bin/sh
#
# LilSmokeOS — post-build hook for SF3000 (real hardware)
#
# Called by Buildroot after the rootfs is populated, before image packing.
# Args: $1 = TARGET_DIR, $2 = BOARD_DIR
#
# Responsibilities:
#   1. Install LilSmokeOS inittab
#   2. Install LilSmokeOS init scripts (same set as qemu-mipsel —
#      real HW shares the entire init flow)
#   3. Install LilSmokeOS config defaults (/etc/lilsmoke/)
#   4. Install DirectFB config (no GPU, fbdev-only, MIPS — no MMX/SSE)
#   5. Install logo + default theme skeleton
#   6. Install the SF3000 GPIO keypad input map
#   7. Create empty mountpoints for /persist, /storage, /overlay, /mnt/sd
#   8. Write /etc/fstab
#   9. Install mdev hotplug helpers
#  10. Install the SF3000 devicetree blob (if available)
#
# Note: most of this is identical to qemu-mipsel/post-build.sh.
# Differences:
#   - SF3000 ships its own input map (sf3000.map)
#   - SF3000 installs a devicetree blob
#   - SF3000 enables /persist OverlayFS by default (no initramfs fallback)

set -eu

TARGET_DIR="$1"
BOARD_DIR="$2"

echo "[lilsmoke] post-build (sf3000): TARGET_DIR=$TARGET_DIR BOARD_DIR=$BOARD_DIR"

# --- 1. inittab ---
install -m 0644 "$BOARD_DIR/inittab" "$TARGET_DIR/etc/inittab"

# --- 2. init scripts ---
install -d "$TARGET_DIR/etc/init.d"
install -m 0755 "$BOARD_DIR/init.d/S01mounts"       "$TARGET_DIR/etc/init.d/S01mounts"
install -m 0755 "$BOARD_DIR/init.d/S02mdev"          "$TARGET_DIR/etc/init.d/S02mdev"
install -m 0755 "$BOARD_DIR/init.d/S03hostname"      "$TARGET_DIR/etc/init.d/S03hostname"
install -m 0755 "$BOARD_DIR/init.d/S05bootcounter"   "$TARGET_DIR/etc/init.d/S05bootcounter"
install -m 0755 "$BOARD_DIR/init.d/S06overlay"       "$TARGET_DIR/etc/init.d/S06overlay"
install -m 0755 "$BOARD_DIR/init.d/S10zram"          "$TARGET_DIR/etc/init.d/S10zram"
install -m 0755 "$BOARD_DIR/init.d/S20network"       "$TARGET_DIR/etc/init.d/S20network"
install -m 0755 "$BOARD_DIR/init.d/S30dropbear"      "$TARGET_DIR/etc/init.d/S30dropbear"
install -m 0755 "$BOARD_DIR/init.d/S40alsa"          "$TARGET_DIR/etc/init.d/S40alsa"
install -m 0755 "$BOARD_DIR/init.d/S50fbtest"        "$TARGET_DIR/etc/init.d/S50fbtest"
install -m 0755 "$BOARD_DIR/init.d/S89bootok"        "$TARGET_DIR/etc/init.d/S89bootok"
install -m 0755 "$BOARD_DIR/init.d/S90lilsmoke-ui"   "$TARGET_DIR/etc/init.d/S90lilsmoke-ui"

# --- 3. LilSmokeOS config defaults ---
install -d "$TARGET_DIR/etc/lilsmoke/input-maps"
install -m 0644 "$BOARD_DIR/lilsmoke.conf"            "$TARGET_DIR/etc/lilsmoke/lilsmoke.conf.default"
install -m 0644 "$BOARD_DIR/input-maps/sf3000.map"    "$TARGET_DIR/etc/lilsmoke/input-maps/sf3000.map"
echo "sf3000" > "$TARGET_DIR/etc/lilsmoke/target"

# --- 4. DirectFB config (no GPU, fbdev-only) ---
install -m 0644 "$BOARD_DIR/directfbrc"               "$TARGET_DIR/etc/directfbrc"

# --- 5. Branding / theme skeleton + logo ---
install -d "$TARGET_DIR/usr/share/lilsmoke/themes/default"
install -m 0644 "$BOARD_DIR/theme.xml"                "$TARGET_DIR/usr/share/lilsmoke/themes/default/theme.xml"
install -d "$TARGET_DIR/usr/share/lilsmoke/themes/default/art"
LILSMOKE_ROOT="$(cd "$BOARD_DIR/../.." && pwd)"
LOGO_SRCDIR="$LILSMOKE_ROOT/assets/logo"
LOGO_PNG="$TARGET_DIR/usr/share/lilsmoke/logo.png"
LOGO_WEBP="$TARGET_DIR/usr/share/lilsmoke/lilsmoke-logo.webp"
install -d "$TARGET_DIR/usr/share/lilsmoke"
if [ -f "$LOGO_SRCDIR/lilsmoke-logo.png" ]; then
    install -m 0644 "$LOGO_SRCDIR/lilsmoke-logo.png" "$LOGO_PNG"
    echo "[lilsmoke] post-build: installed logo.png ($(stat -c%s "$LOGO_PNG") bytes)"
elif [ -f "$LOGO_SRCDIR/lilsmoke-logo.webp" ]; then
    install -m 0644 "$LOGO_SRCDIR/lilsmoke-logo.webp" "$LOGO_WEBP"
    echo "[lilsmoke] post-build: installed lilsmoke-logo.webp (PNG missing — UI must convert)"
fi

# --- 6. Empty mountpoints ---
install -d "$TARGET_DIR/persist"
install -d "$TARGET_DIR/storage"
install -d "$TARGET_DIR/overlay"
install -d "$TARGET_DIR/mnt/sd"

# --- 7. fstab ---
cat > "$TARGET_DIR/etc/fstab" <<EOF
# LilSmokeOS fstab — minimal, mostly handled by init scripts
proc        /proc   proc    defaults                0 0
sysfs       /sys    sysfs   defaults                0 0
devtmpfs    /dev    devtmpfs defaults                0 0
tmpfs       /tmp    tmpfs   defaults,size=8M         0 0
tmpfs       /run    tmpfs   defaults,size=4M         0 0
# LILOS/LILROMS are mounted by lilsmoke-storage via S01mounts.
EOF

# --- 8. mdev.conf ---
cat > "$TARGET_DIR/etc/mdev.conf" <<'EOF'
# Hotplug rules for LilSmokeOS (mdev-based, no udev)
sd[a-z][0-9]+        0:0     0660    @/etc/mdev/hotplug-sd.sh
mmcblk[0-9]+p[0-9]+  0:0     0660    @/etc/mdev/hotplug-sd.sh
event[0-9]+          0:0     0660
js[0-9]+             0:0     0660    @/etc/mdev/hotplug-js.sh
EOF
install -d "$TARGET_DIR/etc/mdev"
install -m 0755 "$BOARD_DIR/mdev/hotplug-sd.sh"   "$TARGET_DIR/etc/mdev/hotplug-sd.sh"
install -m 0755 "$BOARD_DIR/mdev/hotplug-js.sh"   "$TARGET_DIR/etc/mdev/hotplug-js.sh"

# --- 9. BSP status ---
# This userspace profile is useful for cross-building, but the manifest keeps
# the missing display/audio/boot capabilities explicit inside the rootfs.
install -d "$TARGET_DIR/usr/share/lilsmoke"
install -m 0644 "$BOARD_DIR/bsp-status.conf" \
    "$TARGET_DIR/usr/share/lilsmoke/bsp-status.conf"

# --- 10. Profile ---
install -d "$TARGET_DIR/etc/profile.d"
cat > "$TARGET_DIR/etc/profile.d/lilsmoke.sh" <<'EOF'
export LILSMOKE_VERSION="0.7.0-j9"
export LILSMOKE_TARGET="sf3000"
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"
export SDL_VIDEODRIVER="directfb"
export SDL_AUDIODRIVER="alsa"
export DFBARGS="no-hardware,no-sighandler,quiet"
EOF

echo "[lilsmoke] post-build (sf3000): complete"
