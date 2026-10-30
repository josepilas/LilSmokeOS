#!/bin/sh
# All compilation, fixtures, screenshots and logs stay outside production/SDK trees.
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
Q=${QA_MOTION_OUTPUT:-$P/build/theme-motion-20261021}
WORK=${QA_MOTION_WORK:-/root/theme-motion-qa-20261021}
THEMES=${QA_THEMES_ROOT:-/root/lilsmoke-vm-validation/storage/themes}
FONT=${QA_FONT:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}
mkdir -p "$WORK" "$Q"
python3 "$P/tools/ui/test_theme_motion.py" provenance --themes "$THEMES" --output "$Q/provenance.json"
python3 "$P/tools/ui/test_theme_motion.py" snapshot --source "$S" --output "$WORK/source" --manifest "$Q/source-sha256.json"
S=$WORK/source
python3 "$P/tools/ui/test_theme_motion.py" build --source "$S" \
    --harness "$P/tools/ui/theme-motion-qa.cpp" --work "$WORK" --output "$Q/build-host.log"
status=0
python3 "$P/tools/ui/test_theme_motion.py" run --binary "$WORK/theme-motion-qa" \
    --themes "$THEMES" --font "$FONT" --output "$Q" || status=$?
if [ "${QA_MIPS:-0}" = 1 ]; then
    SDK=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
    "$SDK/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -Os -static \
        -Wall -Wextra -Werror -DQA_NUMERIC_ONLY -I"$S" \
        "$P/tools/ui/theme-motion-qa.cpp" -o "$WORK/theme-motion-numeric-mips"
    file "$WORK/theme-motion-numeric-mips" >"$Q/mips-file.txt"
    qemu-mipsel -cpu 74Kf "$WORK/theme-motion-numeric-mips" >"$Q/mips-user-numeric.log" 2>&1
    python3 "$P/tools/ui/test_theme_motion.py" kernel --binary "$WORK/theme-motion-numeric-mips" \
        --busybox "$SDK/target/bin/busybox" --kernel "$SDK/images/vmlinux" --work "$WORK" --output "$Q"
fi
python3 "$P/tools/ui/test_theme_motion.py" verify --source "$P/package/lilsmoke-ui/src/src" \
    --manifest "$Q/source-sha256.json" --output "$Q/source-verification.json"
echo "Host software QA evidence (not an M8 recording or FPS benchmark): $Q"
exit "$status"
