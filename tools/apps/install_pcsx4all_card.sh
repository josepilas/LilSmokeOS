#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
package=${1:-"$P/build/lilsmoke-applications/PCSX4ALL-1.0.6.lilpck"}
[ "$#" -le 1 ] && [ -f "$package" ] || {
    echo 'usage: install_pcsx4all_card.sh [PCSX4ALL.lilpck]' >&2
    exit 2
}
R=/mnt/lilsmoke-spare-roms
mounted=no
mkdir -p "$R"
if ! mountpoint -q "$R"; then mount -t drvfs E: "$R"; mounted=yes; fi
cleanup() { if [ "$mounted" = yes ]; then umount "$R"; fi; }
trap cleanup EXIT HUP INT TERM
# The Windows caller verifies USB identity; this verifies the protected data too.
printf '%s  %s\n' f21fc8017cef062d51013462e72178a25cc2c5e8b30124c1d39a46553adb0af9 \
    "$R/roms/snes/Super Mario World (PT-BR).7z" | sha256sum -c -
LILSMOKE_APPS_ROOT="$R/independent-apps" /root/lilsmoke-applications-20261009/host-helper \
    seed "$package"
sha256sum "$R/independent-apps/PCSX4ALL/runtime/bin/pcsx4all"
sync
