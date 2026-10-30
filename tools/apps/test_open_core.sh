#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-open-core-20261010
mkdir -p "$Q"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$P/package/lilsmoke-scripts/src" \
    "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" -o "$Q/router"
python3 "$P/tools/apps/test_open_core.py" --binary /root/lilsmoke-applications-20261009/host-helper --router "$Q/router"
python3 "$P/tools/apps/test_open_core.py" --binary /root/lilsmoke-applications-20261009/mips-helper --qemu --sysroot "$B/staging"
sh "$P/tools/emulators/test_registry.sh"
python3 "$P/tools/apps/test_packages.py" --binary /root/lilsmoke-applications-20261009/host-helper
python3 "$P/tools/apps/test_packages.py" --binary /root/lilsmoke-applications-20261009/mips-helper --qemu --sysroot "$B/staging"
sh "$P/tools/m8/test-native-duplicates.sh"
sh "$P/tools/m8/test_m8_menu.sh"
