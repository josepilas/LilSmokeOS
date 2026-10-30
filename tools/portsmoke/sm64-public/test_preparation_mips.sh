#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP='/root/sm64-public-candidate/Super Mario 64'
SDK=/root/lilsmoke-os/build/buildroot/output
export QEMU_CPU=74Kf QEMU_LD_PREFIX="$SDK/staging"
export PYTHONHOME="$APP/runtime/python/usr" PYTHONDONTWRITEBYTECODE=1
export LD_LIBRARY_PATH="$APP/runtime/lib:$SDK/staging/usr/lib:$SDK/staging/lib"
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_APP_DIR="$APP"
exec "$APP/runtime/python/usr/bin/python3.11" "$HERE/test_preparation_pipeline.py" \
    --runtime "$APP/runtime" \
    --rom '/path/to/local-input/Super Mario 64 (USA).z64' --out "$1"
