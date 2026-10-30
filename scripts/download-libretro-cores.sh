#!/bin/bash
#
# LilSmokeOS — clone the 8 curated libretro cores
#
# Usage: ./scripts/download-libretro-cores.sh [target_dir]
#   target_dir defaults to ./dl/libretro-cores/
#
# Clones the 8 libretro cores that ship with LilSmokeOS (matches the
# LILSMOKE_CORES list in package/retroarch-mips/retroarch-mips.mk).
# Useful for offline builds or local development.
#
# After running this, you can point the build at your local copies by
# setting in your Buildroot config:
#   BR2_PACKAGE_RETROARCH_MIPS_SITE=/path/to/dl/libretro-cores
#   BR2_PACKAGE_RETROARCH_MIPS_SITE_METHOD=local
#
# The list must match LILSMOKE_CORES in
# package/retroarch-mips/retroarch-mips.mk.

set -euo pipefail

LILSMOKE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET_DIR="${1:-$LILSMOKE_ROOT/dl/libretro-cores}"

# 8 cores — must match LILSMOKE_CORES in
# package/retroarch-mips/retroarch-mips.mk
CORES=(
    "fceumm|https://github.com/libretro/libretro-fceumm.git|master"
    "gambatte|https://github.com/libretro/Gambatte.git|master"
    "mgba|https://github.com/libretro/mgba.git|master"
    "snes9x2005|https://github.com/libretro/snes9x2005.git|master"
    "genesis_plus_gx|https://github.com/libretro/Genesis-Plus-GX.git|master"
    "fbneo|https://github.com/libretro/FBNeo.git|master"
    "mednafen_pce_fast|https://github.com/libretro/beetle-pce-fast-libretro.git|master"
    "handy|https://github.com/libretro/libretro-handy.git|master"
)

mkdir -p "$TARGET_DIR"
cd "$TARGET_DIR"
TARGET_DIR_ABS="$(pwd)"

echo "[download-cores] target: $TARGET_DIR_ABS"
echo "[download-cores] cloning ${#CORES[@]} libretro cores..."
echo ""

SUCCESS=0
FAILED=0
FAILED_LIST=()

for entry in "${CORES[@]}"; do
    name=$(echo "$entry" | cut -d'|' -f1)
    url=$(echo "$entry" | cut -d'|' -f2)
    ref=$(echo "$entry" | cut -d'|' -f3)

    if [ -d "$name/.git" ]; then
        echo "[download-cores] $name: already cloned, pulling..."
        (cd "$name" && git pull --ff-only --quiet) || {
            echo "[download-cores] $name: WARNING — pull failed, keeping existing"
        }
    else
        echo -n "[download-cores] $name: cloning from $url... "
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
echo "[download-cores] done: $SUCCESS cloned, $FAILED failed, ${#CORES[@]} total"

if [ "$FAILED" -gt 0 ]; then
    echo "[download-cores] failed repos:"
    for name in "${FAILED_LIST[@]}"; do
        echo "  - $name"
    done
    exit 1
fi

echo ""
echo "[download-cores] all cores cloned to: $TARGET_DIR_ABS"
echo ""
echo "[download-cores] core → system mapping:"
echo "  fceumm              → NES / Famicom / FDS"
echo "  gambatte            → Game Boy / Game Boy Color"
echo "  mgba                → Game Boy Advance"
echo "  snes9x2005          → SNES / Super Famicom"
echo "  genesis_plus_gx     → Genesis / Mega Drive / SMS / Game Gear"
echo "  fbneo               → Arcade (CPS1, early CPS2, early Neo Geo)"
echo "  mednafen_pce_fast   → PC Engine / TG16"
echo "  handy               → Atari Lynx"
