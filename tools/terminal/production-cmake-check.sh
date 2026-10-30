#!/bin/sh
# Read-only SDK consumer: configure/build the production CMake project outside it.
set -eu
P=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
cmake "-DTEST_ROOT=$B/pkgconfig-path-tests" -P "$P/tools/terminal/pkgconfig_paths_test.cmake"
cmake -S "$P/package/lilsmoke-terminal/src" -B "$B/production-cmake" \
    -DCMAKE_TOOLCHAIN_FILE="$SDK/host/share/buildroot/toolchainfile.cmake" \
    -DPKG_CONFIG_EXECUTABLE="$SDK/host/bin/pkg-config" \
    -DCMAKE_INSTALL_PREFIX=/usr -DLILSMOKE_TERMINAL_TESTS=OFF
cmake --build "$B/production-cmake" --parallel 1
DESTDIR="$B/production-stage" cmake --install "$B/production-cmake"
qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$B/production-cmake/lilsmoke-terminal" --check-pty
echo 'PASS production CMake cross-build/install in isolated output with unchanged SDK pkg-config metadata'
