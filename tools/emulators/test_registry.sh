#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
QA=/root/lilsmoke-emulator-registry-qa
mkdir -p "$QA"
g++ -std=c++17 -O2 -Wall -Wextra -I"$P/package/lilsmoke-scripts/src" \
    "$P/tools/emulators/registry_test.cpp" -o "$QA/registry-test"
"$QA/registry-test"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra \
    -I"$P/package/lilsmoke-scripts/src" "$P/tools/emulators/registry_test.cpp" -o "$QA/registry-test-mips"
# qemu-user does not automatically re-enter itself for fork/exec of a MIPS helper.
# Registering binfmt is unnecessary; this host test exercises the helper protocol.
qemu-mipsel -cpu 74Kf -L "$B/staging" "$QA/registry-test-mips" --no-catalog
sh -n "$P/package/lilsmoke-scripts/src/lilsmoke-launch"
sh -n "$P/package/lilsmoke-scripts/src/lilsmoke-retroarch"
