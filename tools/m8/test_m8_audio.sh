#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD=$(mktemp -d /tmp/lilsmoke-m8-audio-tests.XXXXXX)
trap 'rm -rf -- "$BUILD"' EXIT HUP INT TERM
CC=${CC:-cc}
MIPS_CC=${MIPS_CC:-/root/lilsmoke-os/build/buildroot/output/host/bin/mipsel-buildroot-linux-musl-gcc}
QEMU=${QEMU:-qemu-mipsel}
SRC="$ROOT/board/segam-m8-v6-stock"
TEST="$ROOT/tools/m8/test_m8_audio.c"

"$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 "$TEST" -o "$BUILD/host-test"
"$BUILD/host-test"
"$CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$TEST" -o "$BUILD/sanitizer-test"
"$BUILD/sanitizer-test"
"$MIPS_CC" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -static "$TEST" -o "$BUILD/mips-test"
# Buildroot's 74Kc+DSP2 code requires QEMU's FPU+DSP2-capable 74Kf model.
"$QEMU" -cpu 74Kf "$BUILD/mips-test"
"$MIPS_CC" -std=c11 -Wall -Wextra -Werror -O2 -fPIC -shared -Wl,-z,defs "$SRC/m8-audio.c" -o "$BUILD/m8-audio.so"
"$MIPS_CC" -std=c11 -Wall -Wextra -Werror -Os -static "$SRC/m8-audio.c" "$SRC/m8-audio-play.c" -lm -o "$BUILD/m8-audio-play"
"$QEMU" -cpu 74Kf "$BUILD/m8-audio-play" --describe
if "$QEMU" -cpu 74Kf "$BUILD/m8-audio-play" --tone; then
    echo 'FAIL: helper played without explicit enable' >&2
    exit 1
fi
echo 'PASS: host/sanitizers/MIPS mocks, musl shared backend, static helper, disabled CLI. No hardware accessed.'
