#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=/root/lilsmoke-os/build/buildroot/output
RUNTIME=${SM64_QA_RUNTIME:-'/root/sm64-prep2-payload-20261015-final/Super Mario 64/runtime'}
PROJECT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
export QEMU_CPU=74Kf QEMU_LD_PREFIX="$SDK/staging"
export PYTHONHOME="$RUNTIME/python/usr" PYTHONDONTWRITEBYTECODE=1
export LD_LIBRARY_PATH="$RUNTIME/lib:$SDK/staging/usr/lib:$SDK/staging/lib"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
exec "$RUNTIME/python/usr/bin/python3.11" -u "$HERE/test_device_resume_pipeline.py" \
    --runtime "$RUNTIME" --rom '/path/to/local-input/Super Mario 64 (USA).z64' \
    --legacy-tar "$PROJECT/build/sm64-preparation-fix-20261015/PRIVATE-SD-working.tar" \
    --out "${SM64_QA_OUT:-/root/sm64-prep2-native-qa-20261015}" "$@"
