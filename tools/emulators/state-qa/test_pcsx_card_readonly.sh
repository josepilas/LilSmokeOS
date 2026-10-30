#!/bin/sh
# Read game data only; state fixtures are written to an isolated PC directory.
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
Q=${PCSX_QA_OUTPUT:-/root/pcsx-readonly-20261021}
R=$Q/card
mkdir -p "$R"
if mountpoint -q "$R"; then
    echo 'Refusing an existing, unverified mount' >&2
    exit 2
fi
mount -t drvfs -o ro E: "$R"
cleanup() { umount "$R"; }
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
printf '%s  %s\n' f21fc8017cef062d51013462e72178a25cc2c5e8b30124c1d39a46553adb0af9 \
    "$R/roms/snes/Super Mario World (PT-BR).7z" | sha256sum -c -
export PCSX_QA_OUTPUT="$Q"
python3 "$P/tools/emulators/state-qa/pcsx-recovery-vm.py" \
    --disc "$R/roms/psx/Earthworm2.cue" --menu \
    --cases baseline baseline-enosys normal sync-enosys all-sync-enosys \
    io-error rename-error enospc gpu-oom screenshot-oom readonly
