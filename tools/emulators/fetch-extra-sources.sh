#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export GIT_TERMINAL_PROMPT=0
ROOT=/root/lilsmoke-emulator-expansion-20261008/libretro
mkdir -p "$ROOT"
for spec in \
    nxengine:nxengine-libretro prboom:libretro-prboom \
    stella2014:stella2014-libretro prosystem:prosystem-libretro \
    atari800:libretro-atari800 race:RACE pokemini:PokeMini \
    wswan:beetle-wswan-libretro picodrive:picodrive \
    quicknes:QuickNES_Core snes9x2002:snes9x2002 \
    gpsp:gpsp scummvm:scummvm; do
    name=${spec%%:*}; repo=${spec#*:}
    if [ ! -d "$ROOT/$name/.git" ]; then
        git clone --depth 1 "https://github.com/libretro/$repo.git" "$ROOT/$name"
    fi
    printf '%s|' "$name"
    git -C "$ROOT/$name" rev-parse HEAD
done
