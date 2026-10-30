#!/bin/bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd "$(dirname "$0")/../.." && pwd)
br=/root/lilsmoke-os/build/buildroot
src="$br/output/build/lilsmoke-n64-0bd516ee793bb87b57e9eef19993b37ac0098d74"
cross="$br/output/host/bin/mipsel-buildroot-linux-musl"
out="$root/test-results/n64"
mkdir -p "$out/frames"
cp "$br/output/target/usr/lib/libretro/parallel_n64_libretro.so" "$out/parallel_n64_libretro.so"
"$cross-strip" "$out/parallel_n64_libretro.so"
"$cross-readelf" -h -d "$out/parallel_n64_libretro.so" > "$out/elf.txt"
if grep -E 'NEEDED.*(libGL|GLES|vulkan|X11)' "$out/elf.txt"; then exit 1; fi
"$cross-as" -EB -mips3 "$root/tools/hwtest/n64_fixture.S" -o "$out/fixture.o"
"$cross-objcopy" -O binary -j .text "$out/fixture.o" "$out/fixture.bin"
python3 "$root/tools/hwtest/make_n64_fixture.py" "$out/fixture.bin" "$out/lilsmoke-test.z64"
"$cross-gcc" -O2 -I"$src/libretro-common/include" "$src/tools/lrhost.c" \
    "$src/libretro-common/features/features_cpu.o" \
    "$src/libretro-common/compat/compat_strl.o" -ldl -lpthread -lm -o "$out/lrhost-mips"
cd "$out"
env LRHOST_VERBOSE=1 LRHOST_DUMPDIR="$out/frames" LRHOST_DUMP_FROM=1 LRHOST_DUMP_TO=50 \
    LRHOST_INPUT=15-25:3,30-40:0 \
    timeout 120 qemu-mipsel -cpu 74Kf -L "$br/output/target" \
    "$out/lrhost-mips" "$out/parallel_n64_libretro.so" "$out/lilsmoke-test.z64" 50 \
    parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=cxd4 \
    parallel-n64-cpucore=cached_interpreter parallel-n64-angrylion-multithread=off \
    parallel-n64-angrylion-vioverlay=Unfiltered > "$out/mips-functional.log" 2>&1
for extension in v64 n64; do
    timeout 30 qemu-mipsel -cpu 74Kf -L "$br/output/target" \
        "$out/lrhost-mips" "$out/parallel_n64_libretro.so" "$out/lilsmoke-test.$extension" 5 \
        parallel-n64-gfxplugin=angrylion parallel-n64-rspplugin=cxd4 \
        parallel-n64-cpucore=cached_interpreter parallel-n64-angrylion-multithread=off \
        parallel-n64-angrylion-vioverlay=Unfiltered > "$out/byte-order-$extension.log" 2>&1
    grep -q 'presented 5 duped 0' "$out/byte-order-$extension.log"
done
python3 "$root/tools/hwtest/check_n64_frames.py"
tail -1 "$out/mips-functional.log"
sha256sum "$out/parallel_n64_libretro.so" > "$out/core.sha256"
echo N64_MIPS_FUNCTIONAL_OK
