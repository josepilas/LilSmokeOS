#!/bin/sh
#
# LilSmokeOS — post-build hook for GB350 (real hardware)
#
# GB350 hardware details remain independently unverified. Differences from
# board/sf3000/post-build.sh currently represented by this userspace profile:
#   - Input map: gb350.map instead of sf3000.map
#   - lilsmoke.conf has DISPLAY_ROTATION=90 by default (handled by the
#     config file itself; this script just installs it)

set -eu

TARGET_DIR="$1"
BOARD_DIR="$2"

echo "[lilsmoke] post-build (gb350): TARGET_DIR=$TARGET_DIR BOARD_DIR=$BOARD_DIR"

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
install -m 0644 "$BOARD_DIR/input-maps/gb350.map"     "$TARGET_DIR/etc/lilsmoke/input-maps/gb350.map"
echo "gb350" > "$TARGET_DIR/etc/lilsmoke/target"

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
install -d "$TARGET_DIR/usr/share/lilsmoke"
install -m 0644 "$BOARD_DIR/bsp-status.conf" \
    "$TARGET_DIR/usr/share/lilsmoke/bsp-status.conf"

# --- 10. Profile ---
install -d "$TARGET_DIR/etc/profile.d"
cat > "$TARGET_DIR/etc/profile.d/lilsmoke.sh" <<'EOF'
export LILSMOKE_VERSION="0.7.0-j9"
export LILSMOKE_TARGET="gb350"
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"
export SDL_VIDEODRIVER="directfb"
export SDL_AUDIODRIVER="alsa"
export DFBARGS="no-hardware,no-sighandler,quiet"
EOF

echo "[lilsmoke] post-build (gb350): complete"
