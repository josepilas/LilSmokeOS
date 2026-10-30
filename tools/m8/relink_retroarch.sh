#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R="$B/build/retroarch-mips-v1.19.1"
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
install -m 0644 "$P/package/retroarch-mips/m8_lilsmoke_audio.c" "$R/audio/drivers/"
install -m 0644 "$P/package/retroarch-mips/m8_lilsmoke_input.c" "$R/input/drivers_joypad/"
install -m 0644 "$P/package/retroarch-mips/m8_lilsmoke_input.h" "$R/input/drivers_joypad/"
install -m 0644 "$P/package/lilsmoke-ui/src/src/gamepad-state.h" "$R/input/drivers_joypad/"
install -m 0644 "$P/package/retroarch-mips/m8_sdl2_window.h" "$R/gfx/drivers/"
install -m 0644 "$P/package/retroarch-mips/m8_native_frame.h" "$R/gfx/drivers/"
python3 "$P/tools/m8/check_retroarch_patches.py" \
    --archive "$B/../dl/retroarch-mips/retroarch-mips-v1.19.1-br1.tar.gz" \
    --emit-sdl "$R/gfx/drivers/sdl2_gfx.c" --emit-menu "$R/menu/menu_driver.c"
cd "$R"
make -j2 CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc" CXX="$B/host/bin/mipsel-buildroot-linux-musl-g++"
install -m 0755 retroarch "$B/target/usr/bin/retroarch"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded "$B/target/usr/bin/retroarch"
