#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
Q=${1:?usage: build_qualified_motion.sh NEW_OUTPUT_DIRECTORY}
test ! -e "$Q"
mkdir -p "$Q"
snapshot() {
    find "$P/package/lilsmoke-ui/src/src" "$P/package/lilsmoke-ui/src/apps" "$P/package/lilsmoke-od-sdl/src" \
         "$P/package/lilsmoke-media/src" "$P/package/lilsmoke-scripts/src" \
         -type f \( -name '*.h' -o -name '*.cpp' \) -print0 |
        LC_ALL=C sort -z | xargs -0 sha256sum
    sha256sum "$P/package/lilsmoke-ui/src/Makefile" "$P/package/lilsmoke-ui/lilsmoke-ui.mk" \
        "$P/tools/ui/build_isolated.sh" "$P/tools/ui/build_qualified_motion.sh"
}
snapshot > "$Q/source-inputs.before"
UI_BUILD_OUTPUT="$Q/build" sh "$P/tools/ui/build_isolated.sh"
snapshot > "$Q/source-inputs.after"
cmp "$Q/source-inputs.before" "$Q/source-inputs.after" || {
    echo 'Sources changed during build; output is not qualified.' >&2
    exit 1
}
sha256sum "$Q/build/lilsmoke-ui"
