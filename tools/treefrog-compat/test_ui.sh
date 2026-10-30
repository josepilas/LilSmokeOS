#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
Q=${QA_OUTPUT:-$(mktemp -d /tmp/lilsmoke-treefrog-ui-qa-XXXXXX)}
FONT=${QA_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}
mkdir -p "$Q"
flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
if [ "${SANITIZE:-1}" = 0 ]; then flags='-O2'; fi
echo "Isolated QA output: $Q"
${CXX:-g++} -std=c++17 $flags -Wall -Wextra -Werror \
    -I"$S" -I"$P/package/lilsmoke-scripts/src" -I"$P/package/lilsmoke-usb-gamepads/src" \
    "$P/tools/treefrog-compat/ui_test.cpp" "$S/TreeFrogCatalog.cpp" "$S/TreeFrogPortsView.cpp" \
    "$S/ListView.cpp" "$S/ProcessLaunch.cpp" "$S/Renderer.cpp" "$S/Font.cpp" "$S/Theme.cpp" "$S/Log.cpp" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf json-c) -lwebp -ldl -Wl,--wrap=access -o "$Q/ui-test"
ASAN_OPTIONS=detect_leaks=1 "$Q/ui-test" "$P/tools/treefrog-compat/fixtures" "$FONT" "$Q"
QA_SMALL_SCREEN=1 ASAN_OPTIONS=detect_leaks=1 "$Q/ui-test" "$P/tools/treefrog-compat/fixtures" "$FONT" "$Q"
${CXX:-g++} -std=c++17 $flags -Wall -Wextra -Werror -I"$S" \
    "$P/tools/treefrog-compat/process_launch_test.cpp" "$S/ProcessLaunch.cpp" "$S/Log.cpp" \
    -Wl,--wrap=access -Wl,--wrap=execve -o "$Q/process-launch-test"
ASAN_OPTIONS=detect_leaks=1 "$Q/process-launch-test"
echo 'ProcessLaunch: direct/M8 wrapper argv, missing wrapper and no recursive wrapping passed (exec mocked)'

if [ "${BUILD_UI:-0}" = 1 ]; then
    mkdir -p "$Q/ui"
    cp "$P/package/lilsmoke-ui/src/Makefile" "$Q/ui/"
    cp -R "$S" "$P/package/lilsmoke-ui/src/apps" "$Q/ui/"
    make -C "$Q/ui" -j2 \
        CPPFLAGS="-I$P/package/lilsmoke-scripts/src -I$P/package/lilsmoke-media/src -I$P/package/lilsmoke-usb-gamepads/src"
fi
