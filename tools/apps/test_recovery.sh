#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
Q=/root/lilsmoke-applications-20261009
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DLILSMOKE_APPS_QA \
    -I"$P/package/lilsmoke-scripts/src" "$P/package/lilsmoke-apps/src/lilsmoke-apps.cpp" \
    -larchive -ljson-c -lsodium -o "$Q/qa-helper"
python3 "$P/tools/apps/test_recovery.py" "$Q/qa-helper"
