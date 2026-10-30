#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
set -eu
umask 077
test -n "$LILSMOKE_APP_DIR"
cd -- "$LILSMOKE_APP_DIR"
if [ "$#" -ne 0 ]; then
    echo 'This note example is not an emulator.' >&2
    exit 2
fi
if [ ! -x /usr/bin/lilsmoke-keyboard ]; then
    echo 'The LilSmoke keyboard executable is unavailable.' >&2
    exit 3
fi
# No display/input context is held by this shell while the keyboard owns them.
if note=$(/usr/bin/lilsmoke-keyboard --title 'Developer Note' --max-length 128); then
    mkdir -p userdata
    printf '%s\n' "$note" > userdata/note.txt
    echo 'Saved userdata/note.txt'
else
    status=$?
    [ "$status" -ne 1 ] || exit 0
    exit "$status"
fi
