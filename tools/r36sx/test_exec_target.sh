#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$LILSMOKE_R36_BOOTSTRAP_MODE" = exec ]
[ "$PATH" = /bin:/sbin:/usr/bin:/usr/sbin ]
[ -z "${LD_LIBRARY_PATH:-}" ]
[ -z "${LD_PRELOAD:-}" ]
printf 'PASS MIPS plugin exec replaced process; clean environment\n'
exit 37
