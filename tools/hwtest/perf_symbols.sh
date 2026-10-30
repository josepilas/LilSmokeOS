#!/bin/sh
set -eu
build=/root/lilsmoke-os/build/buildroot/output
out=/root/lilsmoke-performance
nm="$build/host/bin/mipsel-buildroot-linux-musl-nm"
"$nm" -D --defined-only "$build/target/usr/bin/retroarch" | awk '{print $3}' | sort >"$out/ra-symbols"
"$nm" -D --defined-only "$out/runs/candidate-profile/core.so" | awk '{print $3}' | sort >"$out/core-symbols"
comm -12 "$out/ra-symbols" "$out/core-symbols"
grep -R -n 'FE_TONEAREST\|feset\|SET_FPSCR\|fastfp\|fpscr\|fpu_control' "$build/build/retroarch-mips-v1.19.1/retroarch.c" "$build/build/retroarch-mips-v1.19.1/runloop.c" "$build/build/retroarch-mips-v1.19.1/libretro-common" | head -45
