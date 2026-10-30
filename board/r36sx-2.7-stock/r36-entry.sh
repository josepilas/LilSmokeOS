#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Also serves as the icube sentinel for kernels whose initramfs waits for it.
BASE=/mnt/sdcard/lilsmoke
"$BASE/r36-bootlog" "SHELL_HOOK:$0" || true
LILSMOKE_R36_BOOTSTRAP_MODE='exec'
export LILSMOKE_R36_BOOTSTRAP_MODE
exec /bin/sh "$BASE/r36-launch.sh"
