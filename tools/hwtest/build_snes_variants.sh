#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${1:-/root/lilsmoke-os/build/buildroot/output}
work=${2:-/root/lilsmoke-performance}
mkdir -p "$work" "$root/test-results/emulation-performance/cores"
cc="$build/host/bin/mipsel-buildroot-linux-musl-gcc"
for variant in aligned-o2 aligned-o3 aligned-lto; do
    src="$work/$variant"
    mkdir -p "$src"
    cp -a "$root/package/retroarch-mips/cores/snes9x2005/." "$src/"
    case "$variant" in
        aligned-o2) opt=-O2; lto= ;;
        aligned-o3) opt=-O3; lto= ;;
        aligned-lto) opt=-O3; lto=-flto ;;
    esac
    # Preserve upstream includes/defines while making the optimization level
    # the final compiler argument. Never force unaligned scalar dereferences.
    make -C "$src" -f Makefile clean >/dev/null
    make -C "$src" -f Makefile -f "$root/tools/hwtest/snes_benchmark_flags.mk" -j4 \
        platform=unix CC="$cc" CXX="$build/host/bin/mipsel-buildroot-linux-musl-g++" \
        PERF_OPT="$opt" PERF_LTO="$lto" >"$root/test-results/emulation-performance/cores/$variant-build.log" 2>&1
    cp "$src/snes9x2005_libretro.so" "$root/test-results/emulation-performance/cores/$variant.so"
    "$build/host/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded \
        "$root/test-results/emulation-performance/cores/$variant.so"
    echo "BUILT $variant"
done
ulimit -c 0
cd "$root/test-results/emulation-performance/baseline"
set +e
qemu-mipsel -cpu 74Kf -L "$build/target" ./core-benchmark ./core.so game.sfc 60 \
    >../prebuilt-user-mode.log 2>&1
rc=$?
set -e
printf 'Original core qemu-user exit=%s\n' "$rc"
for variant in aligned-o2 aligned-o3 aligned-lto; do
    qemu-mipsel -cpu 74Kf -L "$build/target" ./core-benchmark "../cores/$variant.so" game.sfc 3600 0 \
        >"../cores/$variant-run.log" 2>&1
    tail -1 "../cores/$variant-run.log"
done
