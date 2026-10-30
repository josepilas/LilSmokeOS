#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
Q=${QA_OUTPUT:-$(mktemp -d /tmp/lilsmoke-treefrog-qa-XXXXXX)}
mkdir -p "$Q"
flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
if [ "${SANITIZE:-1}" = 0 ]; then flags='-O2'; fi
echo "Isolated QA output: $Q"
${CXX:-g++} -std=c++17 $flags -Wall -Wextra -Werror \
    -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    "$P/tools/treefrog-compat/catalog_test.cpp" "$S/TreeFrogCatalog.cpp" \
    $(pkg-config --cflags --libs json-c) -o "$Q/catalog-test"
ASAN_OPTIONS=detect_leaks=1 "$Q/catalog-test" "$P/tools/treefrog-compat/fixtures"
