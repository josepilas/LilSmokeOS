#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
work=$(mktemp -d /root/sm64-pcm-test-XXXXXX)
gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$P/package/lilsmoke-sm64-original/src" "$P/tools/gamepads/test_sm64_audio.c" -o "$work/host"
"$work/host"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$P/package/lilsmoke-sm64-original/src" "$P/tools/gamepads/test_sm64_audio.c" -o "$work/mips"
qemu-mipsel -cpu 74Kf -L "$B/staging" "$work/mips"
