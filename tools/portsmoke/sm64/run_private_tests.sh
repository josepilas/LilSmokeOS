#!/bin/bash
set -euo pipefail
T=$(cd -- "$(dirname -- "$0")" && pwd)
B="$T/_build"
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
ROM=${1:-/path/to/local-input/Super Mario 64 (USA).z64}
ASSETS=${ASSETS:-/root/lilsmoke-sm64-base/assets.json}
QEMU_SYSTEM=${QEMU_SYSTEM:-/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel}
gcc_license=$(find "$SDK/build" -maxdepth 2 -path '*/host-gcc-final-*/COPYING.RUNTIME' -print -quit)
gcc_license=$(dirname -- "$gcc_license")
make -j1 -f "$T/test_recipe.mk"
python3 "$T/test_private_rom.py" --rom "$ROM" --assets-json "$ASSETS" \
    --host-binary "$B/host-data" --mips-binary "$B/mips-data" --sysroot "$SDK/staging" --out "$B/results"
for helper in host-helper mips-helper; do
    flags=()
    if [ "$helper" = mips-helper ]; then flags=(--qemu-helper); fi
    python3 "$T/test_lilpck.py" --binary "$B/recipe/src/portsmoke-sm64-data" --gcc-license-dir "$gcc_license" \
        --scene "$B/recipe/src/scene/sm64-scene-dev" \
        --sdl-library "$SDK/target/usr/lib/libSDL2-2.0.so.0" \
        --sdl-license "$SDK/build/sdl2-2.28.5/LICENSE.txt" \
        --helper "/root/lilsmoke-applications-20261009/$helper" --sysroot "$SDK/staging" \
        --rom "$ROM" "${flags[@]}"
done
bash "$T/run_gameplay_tests.sh"
python3 "$T/test_gameplay_private.py" --rom "$ROM" --sdk "$SDK" --build "$B/gameplay"
python3 "$T/test_extraction.py" --rom "$ROM" --sdk "$SDK" --out "$B/extraction-evidence"
python3 "$T/build_scene.py" --sdk "$SDK" --out "$B/play-mips-qa" --qa
python3 "$T/test_native_game.py" --binary "$B/play-mips-qa/sm64-scene-qa" --sdk "$SDK" \
    --rom "$ROM" --out "$B/play-evidence/mips" --pictures
python3 "$T/vm_gameplay_smoke.py" --sdk "$SDK" --qemu "$QEMU_SYSTEM" --qa "$B/play-mips-qa/sm64-scene-qa" \
    --package "$B/SM64-BYO-Data-Development-0.4.0.lilpck" --rom "$ROM" --out "$B/play-evidence/vm-release"
printf 'PASS private-ROM, lifecycle, safe extraction/deletion, native movement and DirectFB/QMP tests\n'
