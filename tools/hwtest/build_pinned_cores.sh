#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
prefix="$build/host/bin/mipsel-buildroot-linux-musl-"
export CC="${prefix}gcc" CXX="${prefix}g++" AR="${prefix}ar"
export RANLIB="${prefix}ranlib" STRIP="${prefix}strip" READELF="${prefix}readelf"
export CFLAGS='-O3 -g0 -DLSB_FIRST -DHAVE_MIPS_DSP -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt'
export CXXFLAGS="$CFLAGS" LDFLAGS='' JOBS=4
exec sh "$root/package/retroarch-mips/build-cores.sh" \
    /root/lilsmoke-performance/pinned-cores "$root/test-results/emulation-performance/pinned-cores" "$@"
