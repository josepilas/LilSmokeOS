#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
package=$P/build/lilsmoke-applications/PCSX4ALL-1.0.2.lilpck
test -f "$package"
rm -f -- "$B/target/usr/share/lilsmoke/builtin-apps/PCSX4ALL-1.0.0.lilpck" "$B/target/usr/share/lilsmoke/builtin-apps/PCSX4ALL-1.0.1.lilpck"
install -D -m 0644 "$package" "$B/target/usr/share/lilsmoke/builtin-apps/PCSX4ALL-1.0.2.lilpck"
install -m 0755 "$P/package/lilsmoke-apps/src/S08applications" "$B/target/etc/init.d/S08applications"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-od-launch" "$B/target/usr/lib/lilsmoke/lilsmoke-od-launch"
# The engine is now inside its application archive, not a second native entry.
test ! -L "$B/target/usr/bin/pcsx4all-launch"
rm -f -- "$B/target/usr/bin/pcsx4all-launch"
sh -n "$P/package/pcsx4all/app/PCSX4ALL.sh"
sh -n "$B/target/etc/init.d/S08applications"
