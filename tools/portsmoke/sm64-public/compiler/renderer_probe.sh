#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
WORK=/root/lilsmoke-public-compiler
SOURCE=/root/lilsmoke-sm64-original-private-iig2ryae
SDK=/root/lilsmoke-os/build/buildroot/output
export QEMU_LD_PREFIX="$SDK/staging" QEMU_CPU=74Kf
cd "$SOURCE"
/usr/bin/time -v "$WORK/runtime/wrappers/cc" \
    -O3 -mips32r2 -mabi=32 -mhard-float -mfp32 -mdspr2 \
    -fno-fast-math -fno-strict-aliasing -fwrapv -fcommon -fno-math-errno \
    -D_LANGUAGE_C -DVERSION_US -DNOWUP -DNON_MATCHING -DAVOID_UB -DTARGET_LINUX \
    -DNO_SEGMENTED_MEMORY -DENABLE_SOFTRAST -DF3DEX_GBI_2E -DCONVERT \
    -nostdinc -isystem "$WORK/runtime/usr/lib/gcc/mipsel-linux-gnu/12/include" \
    -isystem "$SDK/staging/usr/include" -I"$SDK/staging/usr/include/SDL2" \
    -Iinclude -Isrc -I. -Ibuild/original-mips/include -Ibuild/original-mips \
    -c src/pc/gfx/gfx_soft.c -o "$WORK/probe/private-gfx-probe.o" \
    > "$WORK/probe/renderer.log" 2> "$WORK/probe/renderer-time.log"
printf '%s\n' 'PASS_NATIVE_RENDERER_COMPILE_ONLY_NOT_GAMEPLAY'
