#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
ROOT=/root/lilsmoke-emulator-expansion-20261008
export CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc"
export CXX="$B/host/bin/mipsel-buildroot-linux-musl-g++"
export AR="$B/host/bin/mipsel-buildroot-linux-musl-ar"
export RANLIB="$B/host/bin/mipsel-buildroot-linux-musl-ranlib"
export STRIP="$B/host/bin/mipsel-buildroot-linux-musl-strip"
export READELF="$B/host/bin/mipsel-buildroot-linux-musl-readelf"
export CFLAGS='-O3 -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -DHAVE_MIPS_DSP -ffunction-sections -fdata-sections'
export CXXFLAGS="$CFLAGS"
export LDFLAGS='-Wl,--gc-sections'
export JOBS=${LILSMOKE_BUILD_JOBS:-1}
export LILSMOKE_CORE_LOCK="$P/package/lilsmoke-libretro-extra/cores.lock"
mkdir -p "$ROOT/built-libretro" "$ROOT/logs"
if [ "$#" -eq 0 ]; then set -- nxengine prboom stella2014 prosystem atari800 race pokemini wswan picodrive quicknes snes9x2002 gpsp pcsx_rearmed; fi
for core do
    sh "$P/package/retroarch-mips/build-cores.sh" "$ROOT/libretro" "$ROOT/built-libretro" "$core" \
        >"$ROOT/logs/libretro-$core.log" 2>&1 || {
            tail -n 35 "$ROOT/logs/libretro-$core.log"; exit 1;
        }
    tail -n 2 "$ROOT/logs/libretro-$core.log"
done
