#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP='/root/sm64-public-candidate/Super Mario 64'
SDK=/root/lilsmoke-os/build/buildroot/output
COMPILER=/root/lilsmoke-public-compiler/runtime
mkdir -p "$APP/original"
cp "$HERE/native_build.py" "$APP/runtime/native_build.py"
cp "$HERE/cook.py" "$APP/runtime/cook.py"
for name in cc gcc cpp cc1 as ld objcopy collect2 make; do
    cp "$COMPILER/wrappers/$name" "$APP/runtime/compiler/wrappers/$name"
done
cp "$COMPILER/usr/bin/mipsel-linux-gnu-objcopy" "$APP/runtime/compiler/usr/bin/"
if [ ! -f "$APP/.sm64-cache/current.json" ] && [ ! -f "$APP/original/Arbitrary owner dump name.z64" ]; then
    cp '/path/to/local-input/Super Mario 64 (USA).z64' "$APP/original/Arbitrary owner dump name.z64"
fi
export QEMU_CPU=74Kf QEMU_LD_PREFIX="$SDK/staging"
export LILSMOKE_APP_DIR="$APP" PYTHONHOME="$APP/runtime/python/usr"
export LD_LIBRARY_PATH="$APP/runtime/lib:$SDK/staging/usr/lib:$SDK/staging/lib"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
exec "$APP/runtime/python/usr/bin/python3.11" -u "$APP/runtime/native_build.py" --app "$APP" \
    > /root/sm64-public-candidate/cook-progress.log 2>&1
