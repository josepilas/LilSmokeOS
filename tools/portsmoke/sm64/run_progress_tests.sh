#!/bin/bash
set -euo pipefail
P=$(cd "$(dirname "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-sm64-progress-20261010
S="$P/package/portsmoke-sm64/src"
sources=("$P/tools/portsmoke/sm64/test_progress.cpp" "$S/course_objects.cpp" "$S/sound_effects.cpp"
    "$S/extraction.cpp" "$S/gameplay.cpp" "$S/mario_actor.cpp" "$S/bob_scene.cpp" "$S/asset_pack.cpp" "$S/runtime_assets.cpp")
mkdir -p "$Q/tests"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$S" $(sdl2-config --cflags) "${sources[@]}" -lSDL2 -o "$Q/tests/host"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror -I"$S" \
    -I"$B/staging/usr/include/SDL2" "${sources[@]}" -static-libstdc++ -static-libgcc -lSDL2 -o "$Q/tests/mips"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$S" $(sdl2-config --cflags) "${sources[@]}" -lSDL2 -o "$Q/tests/asan"
python3 "$P/tools/portsmoke/sm64/test_progress.py" --root "$Q" --rom '/path/to/local-input/Super Mario 64 (USA).z64' --sdk "$B"
