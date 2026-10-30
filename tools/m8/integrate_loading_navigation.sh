#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Install only qualified generated artifacts into the development target.
# Never accesses a physical card or a complete disk image.
set -eu
[ "$#" = 3 ] || { echo 'usage: integrate_loading_navigation.sh UI_BINARY APPS_ROUTER PCSX_PACKAGE' >&2; exit 2; }
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
test -x "$B/host/bin/mipsel-buildroot-linux-musl-readelf"
for binary in "$1" "$2"; do
    test -x "$binary"
    "$B/host/bin/mipsel-buildroot-linux-musl-readelf" -h "$binary" | grep -q 'MIPS'
done
test -s "$3"
install -m 0755 "$1" "$B/target/usr/bin/lilsmoke-ui"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$B/target/usr/bin/lilsmoke-ui"
install -m 0755 "$2" "$B/target/usr/lib/lilsmoke/lilsmoke-apps"
for name in lilsmoke-launch lilsmoke-od-launch lilsmoke-retroarch m8-loading-run; do
    sh -n "$P/package/lilsmoke-scripts/src/$name"
    install -m 0755 "$P/package/lilsmoke-scripts/src/$name" "$B/target/usr/lib/lilsmoke/$name"
done
install -m 0644 "$P/package/retroarch-mips/retroarch.cfg" "$B/target/etc/retroarch.cfg"
install -m 0755 "$P/package/lilsmoke-apps/src/S08applications" "$B/target/etc/init.d/S08applications"
install -m 0644 "$3" "$B/target/usr/share/lilsmoke/builtin-apps/PCSX4ALL-1.0.6.lilpck"
# These exact generated archives have been superseded, not source/backups.
for version in 1.0.0 1.0.1 1.0.2 1.0.3 1.0.4 1.0.5; do
    rm -f -- "$B/target/usr/share/lilsmoke/builtin-apps/PCSX4ALL-$version.lilpck"
done
echo 'Development target integration complete; no physical device writes.'
