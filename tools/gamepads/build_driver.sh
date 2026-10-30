#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=gnu11 -O2 -Wall -Wextra \
    -I"$B/staging/usr/include/SDL2" -I"$P/package/lilsmoke-ui/src/src" \
    "$P/package/lilsmoke-usb-gamepads/src/usb-gamepads.c" -lSDL2 -o "$B/target/usr/bin/lilsmoke-usb-gamepads"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$B/target/usr/bin/lilsmoke-usb-gamepads"
install -m 0755 "$P/package/lilsmoke-usb-gamepads/S07usb-gamepads" "$B/target/etc/init.d/"
install -m 0755 "$P/package/lilsmoke-usb-gamepads/src/lilsmoke-input-diagnostics" "$B/target/usr/bin/"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=gnu11 -O2 \
    -I"$B/staging/usr/include/libusb-1.0" "$P/package/lilsmoke-usb-gamepads/src/usb-probe.c" \
    -lusb-1.0 -o "$B/target/usr/bin/lilsmoke-usb-probe"
