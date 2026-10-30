#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${QA_OUTPUT:-/root/lilsmoke-frame-pacer-20261021}
mkdir -p "$Q"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$P/package/lilsmoke-ui/src/src" "$P/tools/ui/frame-pacer-test.cpp" \
    -o "$Q/host"
ASAN_OPTIONS=detect_leaks=1 "$Q/host"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$P/package/lilsmoke-ui/src/src" "$P/tools/ui/frame-pacer-test.cpp" \
    -o "$Q/mips"
qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/mips"
sha256sum "$P/package/lilsmoke-ui/src/src/FramePacer.h" \
    "$P/tools/ui/frame-pacer-test.cpp" "$Q/host" "$Q/mips" > "$Q/inputs.sha256"
