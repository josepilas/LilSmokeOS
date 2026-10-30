#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build=${BUILDROOT_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
source_dir="$build/build/directfb-1.7.7"
if ! grep -q linux_input_event_wire_abi_check "$source_dir/inputdrivers/linux_input/linux_input.c"; then
    patch --batch --forward -d "$source_dir" -p1 <"$root/patches/directfb/0004-check-linux-input-wire-abi.patch"
fi
make -C "$source_dir/inputdrivers/linux_input" clean
make -C "$source_dir/inputdrivers/linux_input" -j2
cp "$source_dir/inputdrivers/linux_input/.libs/libdirectfb_linux_input.so" \
    "$root/test-results/emulation-performance/libdirectfb_linux_input-abi.so"
echo DIRECTFB_INPUT_ABI_BUILT
