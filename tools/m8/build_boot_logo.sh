#!/bin/sh
set -eu
if [ "$#" != 3 ]; then
    echo "usage: $0 SDK_OUTPUT NEW_OUTPUT_DIR ORIGINAL_LOGO.webp" >&2
    exit 2
fi
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SDK=$(CDPATH= cd -- "$1" && pwd)
LOGO=$(realpath -- "$3")
OUT=$2
CC=$SDK/host/bin/mipsel-buildroot-linux-musl-gcc
STRIP=$SDK/host/bin/mipsel-buildroot-linux-musl-strip
BOARD=$P/board/segam-m8-v6-stock
test -x "$CC"
test -f "$LOGO"
# A new isolated output only; never install into the shared SDK or an image.
mkdir -- "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
mkdir "$OUT/payload"
cp -- "$LOGO" "$OUT/logo-source.webp"
cc -std=c11 -O2 -Wall -Wextra -Werror "$P/tools/m8/pack_boot_logo.c" \
    $(pkg-config --cflags --libs libwebp) -o "$OUT/pack-boot-logo"
"$OUT/pack-boot-logo" "$OUT/logo-source.webp" "$OUT/payload/boot-logo.argb"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float \
    -static "$BOARD/m8-boot-logo.c" -o "$OUT/payload/m8-boot-logo"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float \
    -DM8_BOOT_CORE_ONLY -shared -fPIC -Wl,-z,defs \
    "$BOARD/m8-boot-logo.c" "$BOARD/m8-boot-logo-gate.c" -ldl -pthread -o "$OUT/payload/libm8-boot-logo.so"
"$STRIP" --strip-unneeded "$OUT/payload/m8-boot-logo" "$OUT/payload/libm8-boot-logo.so"
cp "$BOARD/m8-launch.sh" "$OUT/payload/m8-launch.sh"
chmod 0755 "$OUT/payload/m8-boot-logo" "$OUT/payload/m8-launch.sh"
chmod 0644 "$OUT/payload/libm8-boot-logo.so" "$OUT/payload/boot-logo.argb"
cd "$OUT"
sha256sum logo-source.webp payload/* > SHA256SUMS
printf 'Boot-logo payload only: %s/payload\n' "$OUT"
