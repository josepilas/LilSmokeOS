#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
S=$B/build/lilsmoke-ui-0.1.0
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
test -f "$S/src/main.o"
cp "$P/package/lilsmoke-ui/src/src/Input.h" "$S/src/Input.h"
cp "$P/package/lilsmoke-ui/src/src/gamepad-state.h" "$S/src/gamepad-state.h"
for file in main Launcher EmulatorSettingsView SettingsView RetroArchPlaylists Input; do
    cp "$P/package/lilsmoke-ui/src/src/$file.cpp" "$S/src/$file.cpp"
    "$CXX" -std=c++17 -O3 -flto -Wall -ffunction-sections -fdata-sections \
        -I"$B/staging/usr/include/SDL2" -I"$P/package/lilsmoke-scripts/src" \
        -c "$S/src/$file.cpp" -o "$S/src/$file.o"
done
"$CXX" -flto -Wl,--gc-sections -s -o "$S/lilsmoke-ui" "$S/src/"*.o \
    -lSDL2_image -lSDL2_ttf -lSDL2 -lz -ljson-c -lwebp
install -m 0755 "$S/lilsmoke-ui" "$B/target/usr/bin/lilsmoke-ui"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -s \
    -I"$P/package/lilsmoke-scripts/src" "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" \
    -o "$B/target/usr/lib/lilsmoke/lilsmoke-emulators"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-launch" "$B/target/usr/lib/lilsmoke/lilsmoke-launch"
ln -sf ../lib/lilsmoke/lilsmoke-emulators "$B/target/usr/bin/lilsmoke-emulators"
ln -sf ../lib/lilsmoke/lilsmoke-launch "$B/target/usr/bin/lilsmoke-launch"
sh -n "$B/target/usr/lib/lilsmoke/lilsmoke-launch"
