#!/bin/bash
#
# LilSmokeOS — clone all 50 OpenDingux emulators
#
# Usage: ./scripts/download-emulators.sh [target_dir]
#   target_dir defaults to ./dl/opendingux-emulators/
#
# Clones all 50 OpenDingux emulator repos at the same git refs that
# the build uses. This is useful for:
#   - Offline builds (the Buildroot build step uses git clone, which
#     requires network access — pre-cloning lets you build offline)
#   - Development (you can edit the emulator source locally and
#     point the build at your local copy via a site override)
#   - Auditing (you can review every line of code that ends up in
#     your image)
#
# After running this, set in your Buildroot config:
#   BR2_PACKAGE_OPENDINGUX_EMULATORS_SITE=/path/to/dl/opendingux-emulators
#   BR2_PACKAGE_OPENDINGUX_EMULATORS_SITE_METHOD=local
# to use the pre-cloned copies instead of fetching from GitHub.
#
# The list of repos must match LILSMOKE_OD_EMULATORS in
# package/opendingux-emulators/opendingux-emulators.mk. If you add a
# new emulator to the .mk file, add it here too.

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET_DIR="${1:-$LILSMOKE_ROOT/dl/opendingux-emulators}"

# 50 emulators — must match LILSMOKE_OD_EMULATORS in
# package/opendingux-emulators/opendingux-emulators.mk
EMULATORS=(
    "gambatte|https://github.com/OpenDingux/gambatte.git|master"
    "mgba|https://github.com/OpenDingux/mgba.git|master"
    "snes9x|https://github.com/OpenDingux/snes9x.git|master"
    "fceux|https://github.com/OpenDingux/fceux.git|master"
    "picodrive|https://github.com/OpenDingux/picodrive.git|master"
    "mednafen_pce|https://github.com/OpenDingux/mednafen-pce.git|master"
    "stella|https://github.com/OpenDingux/stella.git|master"
    "dosbox|https://github.com/OpenDingux/dosbox.git|master"
    "atari800|https://github.com/OpenDingux/atari800.git|master"
    "handy|https://github.com/OpenDingux/handy.git|master"
    "caprice|https://github.com/OpenDingux/caprice.git|master"
    "fuse|https://github.com/OpenDingux/fuse.git|master"
    "gpsp|https://github.com/OpenDingux/gpsp.git|master"
    "nestopia|https://github.com/OpenDingux/nestopia.git|master"
    "openmsx|https://github.com/OpenDingux/openmsx.git|master"
    "neopop|https://github.com/OpenDingux/neopop.git|master"
    "oswan|https://github.com/OpenDingux/oswan.git|master"
    "pocketsnes|https://github.com/OpenDingux/pocketsnes.git|master"
    "scummvm|https://github.com/OpenDingux/scummvm.git|master"
    "vice|https://github.com/OpenDingux/vice.git|master"
    "mednafen_ngp|https://github.com/OpenDingux/mednafen-ngp.git|master"
    "mednafen_pce_fast|https://github.com/OpenDingux/mednafen-pce-fast.git|master"
    "mednafen_psx|https://github.com/OpenDingux/mednafen-psx.git|master"
    "mednafen_snes|https://github.com/OpenDingux/mednafen-snes.git|master"
    "mednafen_vb|https://github.com/OpenDingux/mednafen-vb.git|master"
    "mednafen_wswan|https://github.com/OpenDingux/mednafen-wswan.git|master"
    "genesis_plus_gx|https://github.com/OpenDingux/genesis-plus-gx.git|master"
    "fbalpha|https://github.com/OpenDingux/fbalpha.git|master"
    "fbneo|https://github.com/OpenDingux/fbneo.git|master"
    "fceumm|https://github.com/OpenDingux/fceumm.git|master"
    "gambatte_qt|https://github.com/OpenDingux/gambatte-qt.git|master"
    "gngb|https://github.com/OpenDingux/gngb.git|master"
    "gnuboy|https://github.com/OpenDingux/gnuboy.git|master"
    "gspca|https://github.com/OpenDingux/gspca.git|master"
    "hcl|https://github.com/OpenDingux/hcl.git|master"
    "mednafen_gba|https://github.com/OpenDingux/mednafen-gba.git|master"
    "mednafen_nes|https://github.com/OpenDingux/mednafen-nes.git|master"
    "mgba_qt|https://github.com/OpenDingux/mgba-qt.git|master"
    "pcsx4all_standalone|https://github.com/OpenDingux/pcsx4all.git|master"
    "picodrive_2017|https://github.com/OpenDingux/picodrive-2017.git|master"
    "sdlpop|https://github.com/OpenDingux/sdlpop.git|master"
    "smw|https://github.com/OpenDingux/smw.git|master"
    "snes9x_2010|https://github.com/OpenDingux/snes9x-2010.git|master"
    "supertux|https://github.com/OpenDingux/supertux.git|master"
    "tyrquake|https://github.com/OpenDingux/tyrquake.git|master"
    "uqm|https://github.com/OpenDingux/uqm.git|master"
    "voxelstein|https://github.com/OpenDingux/voxelstein.git|master"
    "xrick|https://github.com/OpenDingux/xrick.git|master"
    "zemulator|https://github.com/OpenDingux/zemulator.git|master"
)

mkdir -p "$TARGET_DIR"
cd "$TARGET_DIR"
TARGET_DIR_ABS="$(pwd)"

echo "[download-emulators] target: $TARGET_DIR_ABS"
echo "[download-emulators] cloning ${#EMULATORS[@]} emulators..."
echo ""

SUCCESS=0
FAILED=0
FAILED_LIST=()

for entry in "${EMULATORS[@]}"; do
    name=$(echo "$entry" | cut -d'|' -f1)
    url=$(echo "$entry" | cut -d'|' -f2)
    ref=$(echo "$entry" | cut -d'|' -f3)

    if [ -d "$name/.git" ]; then
        echo "[download-emulators] $name: already cloned, pulling..."
        (cd "$name" && git pull --ff-only --quiet) || {
            echo "[download-emulators] $name: WARNING — pull failed, keeping existing"
        }
    else
        echo -n "[download-emulators] $name: cloning from $url... "
        if git clone --depth 1 --branch "$ref" "$url" "$name" 2>/dev/null; then
            echo "OK"
            SUCCESS=$((SUCCESS + 1))
        else
            echo "FAILED"
            FAILED=$((FAILED + 1))
            FAILED_LIST+=("$name")
        fi
    fi
done

echo ""
echo "[download-emulators] done: $SUCCESS cloned, $FAILED failed, ${#EMULATORS[@]} total"

if [ "$FAILED" -gt 0 ]; then
    echo "[download-emulators] failed repos:"
    for name in "${FAILED_LIST[@]}"; do
        echo "  - $name"
    done
    exit 1
fi

echo ""
echo "[download-emulators] all emulators cloned to: $TARGET_DIR_ABS"
echo "[download-emulators] to use these in the build, add to your defconfig:"
echo "  BR2_PACKAGE_OPENDINGUX_EMULATORS_SITE=\"$TARGET_DIR_ABS\""
echo "  BR2_PACKAGE_OPENDINGUX_EMULATORS_SITE_METHOD=local"
