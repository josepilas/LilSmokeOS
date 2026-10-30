#!/bin/sh

set -eu

THEMES_ROOT="${1:?usage: run_theme_preflight.sh <themes-root> <output-root> [renderer]}"
OUTPUT_ROOT="${2:?usage: run_theme_preflight.sh <themes-root> <output-root> [renderer]}"
RENDERER="${3:-/tmp/theme_render_smoke}"
FONT="${LILSMOKE_TEST_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}"

mkdir -p "$OUTPUT_ROOT"
count=0
for theme in "$THEMES_ROOT"/*; do
    [ -d "$theme" ] || continue
    [ -f "$theme/theme.xml" ] || continue
    name="$(basename "$theme")"
    echo "[theme-preflight] $name"
    mkdir -p "$OUTPUT_ROOT/$name"
    SDL_VIDEODRIVER=dummy "$RENDERER" \
        "$theme" "$OUTPUT_ROOT/$name" "$FONT"
    count=$((count + 1))
done

[ "$count" -gt 0 ] || {
    echo "theme-preflight: no themes found under $THEMES_ROOT" >&2
    exit 1
}

echo "THEME_PREFLIGHT_OK themes=$count images=$((count * 6))"
