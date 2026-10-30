#!/bin/sh
set -eu
: "${LILSMOKE_OD_SYSROOT:?set target sysroot}"
case "${1:-}" in
    --version) printf '%s\n' 1.2.68 ;;
    --cflags) printf '%s\n' "-I$LILSMOKE_OD_SYSROOT/usr/include/SDL -D_REENTRANT" ;;
    --libs) printf '%s\n' "-L$LILSMOKE_OD_SYSROOT/usr/lib -lSDL -pthread" ;;
    --prefix|--exec-prefix) printf '%s\n' "$LILSMOKE_OD_SYSROOT/usr" ;;
    *) exit 1 ;;
esac
