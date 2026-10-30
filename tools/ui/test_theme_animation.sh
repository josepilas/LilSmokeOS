#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${QA_OUTPUT:-$(mktemp -d /tmp/lilsmoke-theme-animation-XXXXXX)}
mkdir -p "$Q"
echo "Isolated storyboard QA output: $Q"
set -- "$P/tools/ui/test_theme_animation.cpp" "$S/Theme.cpp" "$S/Log.cpp"
WRAP='-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc'
"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
    -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=address,undefined \
    -DTHEME_ANIMATION_ASAN -I"$S" "$@" $WRAP -o "$Q/host"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$Q/host" "$Q/host-fixtures"
if [ "${QA_MIPS:-1}" = 1 ]; then
    "$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
        -I"$S" "$@" $WRAP -o "$Q/mips"
    file "$Q/mips"
    qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/mips" "$Q/mips-fixtures"
fi
echo "No renderer, shared SDK, target, image, or SD writes were performed."
