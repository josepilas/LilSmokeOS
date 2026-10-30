#!/bin/bash
# Mechanical pixel resize/encoding only; preserve the supplied artwork.
set -euo pipefail
T=$(cd -- "$(dirname -- "$0")" && pwd)
P=$(cd -- "$T/../../.." && pwd)
test "$#" -eq 1
test -f "$1"
ffmpeg -y -loglevel error -i "$1" -vf 'scale=128:128:flags=area' -frames:v 1 \
    -c:v libwebp -lossless 1 "$P/package/portsmoke-sm64/src/icon.webp"
