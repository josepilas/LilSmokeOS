#!/bin/sh
# Reuse source-qualified production objects; no window, card or host input.
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
UI=${QA_UI_BUILD:-/root/lilsmoke-ui-candidate41-qualified-20261022}
Q=${QA_OUTPUT:-/root/lilsmoke-theme-background-mips-20261022}
mkdir -p "$Q"
cmp "$UI/source-inputs.before" "$UI/source-inputs.after"
sha256sum -c "$UI/source-inputs.after" >"$Q/source-verification.log"
set --
for name in Renderer Font Theme ThemeRender SystemView GridView Log; do
    test -f "$UI/build/$name.o"
    set -- "$@" "$UI/build/$name.o"
done
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -flto -Wall -Wextra -Werror \
    -I"$P/package/lilsmoke-ui/src/src" -I"$B/staging/usr/include" \
    "$P/tools/ui/theme-motion-qa.cpp" "$@" \
    -lSDL2_image -lSDL2_ttf -lSDL2 -ljson-c -lwebp -ldl -Wl,--wrap=SDL_GetTicks -o "$Q/background-qa"
for size in 320x240 856x480 1280x720; do
    qemu-mipsel -cpu 74Kf -L "$B/target" "$Q/background-qa" unused "$Q/$size" \
        "$B/target/usr/share/fonts/dejavu/DejaVuSans.ttf" "${size%x*}" "${size#*x}" background \
        >"$Q/$size.log" 2>&1
    grep THEME_BACKGROUND_OK "$Q/$size.log"
done
sha256sum "$Q/background-qa" "$P/tools/ui/theme-motion-qa.cpp" "$P/tools/ui/theme-background-qa.h" >"$Q/inputs.sha256"
echo 'MIPS_BACKGROUND_REGRESSION_OK software_surface=1 physical_fps=0'
