#!/bin/sh
set -eu
cd -- "${LILSMOKE_APP_DIR:?Application launcher required}"
export LD_LIBRARY_PATH="$PWD/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./runtime/bin/button-tester
