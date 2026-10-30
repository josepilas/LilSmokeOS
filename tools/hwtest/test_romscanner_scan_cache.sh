#!/bin/sh
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d /tmp/lilsmoke-scanner-build-XXXXXX)
trap 'rm -f "$build/scan-cache" "$build/metadata"; rmdir "$build"' EXIT HUP INT TERM
src="$repo/package/lilsmoke-ui/src/src"
for test in romscanner_scan_cache_smoke romscanner_metadata_smoke; do
    name=scan-cache
    [ "$test" != romscanner_metadata_smoke ] || name=metadata
    c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
        -fno-omit-frame-pointer -I"$src" -I"$repo/package/lilsmoke-scripts/src" "$repo/tools/hwtest/$test.cpp" \
        "$src/ROMScanner.cpp" "$src/Log.cpp" -o "$build/$name"
    "$build/$name"
done
