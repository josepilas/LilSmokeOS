#!/bin/bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$(dirname "$0")/../.."
out="$PWD/test-results/syspck"
sources=()
for source in package/lilsmoke-ui/src/src/*.cpp; do
    case "$source" in */main.cpp) ;; *) sources+=("$source") ;; esac
done
read -r -a flags <<< "$(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf libpng)"
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Ipackage/lilsmoke-ui/src/src \
    tools/hwtest/syspck_ui_smoke.cpp "${sources[@]}" "${flags[@]}" -lz -o "$out/ui-smoke"
# The absolute /usr/bin command is installed only inside a private mount namespace.
unshare --mount --propagation private bash -s -- "$out" <<'SANDBOX'
set -euo pipefail
out=$1
work=$(mktemp -d /root/syspck-ui-XXXXXX)
trap 'umount /usr/bin 2>/dev/null || true; rm -rf -- "$work"' EXIT
mkdir -p "$work"/{upper,overlay-work,fixture,keys,empty,persist/config}
chmod 0755 "$work/empty"
cp "$out/lilsmoke-syspck-host" "$work/upper/lilsmoke-syspck"
cp "$out/examples/qa-safe-profile.syspck" "$work/fixture/a 'quoted' safe.syspck"
cp "$out/examples/qa-policy.conf" "$work/policy.conf"
cp "$out/examples/UNTRUSTED-qa.pub" "$work/keys/qa.pub"
cp "$out/examples/qa-policy.conf" "$work/fixture/z-invalid.syspck"
touch "$work/persist/config/lilsmoke.conf"
mount -t overlay overlay -o "lowerdir=/usr/bin,upperdir=$work/upper,workdir=$work/overlay-work" /usr/bin
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
LILSMOKE_SYSPCK_POLICY="$work/policy.conf" LILSMOKE_SYSPCK_KEYS="$work/keys" \
LILSMOKE_SYSPCK_PERSIST="$work/persist" LILSMOKE_SYSPCK_EMPTY="$work/empty" \
    "$out/ui-smoke" "$work/fixture" "$work/persist" "$out/screens"
SANDBOX
echo SYSPCK_UI_VALIDATION_OK
