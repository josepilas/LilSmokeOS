#!/usr/bin/env bash
if [ "${OD_ENGINE_SNAPSHOT:-0}" != 1 ]; then
    export OD_ENGINE_SNAPSHOT=1
    exec bash -c "$(cat "$0")" "$0" "$@"
fi
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PROJECT=$(cd "$HERE/../../.." && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
STAGING=$SDK/mipsel-buildroot-linux-musl/sysroot
export OD_SDK=$SDK OD_SYSROOT=$ROOT/engine-sysroot
export PATH="$SDK/bin:$PATH"
export CC=$SDK/bin/mipsel-buildroot-linux-musl-gcc
export CXX=$SDK/bin/mipsel-buildroot-linux-musl-g++
export AR=$SDK/bin/mipsel-buildroot-linux-musl-ar
export RANLIB=$SDK/bin/mipsel-buildroot-linux-musl-ranlib
export STRIP=$SDK/bin/mipsel-buildroot-linux-musl-strip
export STRINGS=$SDK/bin/mipsel-buildroot-linux-musl-strings
export CFLAGS='-Os -march=mips32r2 -mtune=74kc -mabi=32 -mhard-float -mfp32 -mdspr2 -ffunction-sections -fdata-sections'
export CXXFLAGS=$CFLAGS
export CPPFLAGS="-isystem $OD_SYSROOT/usr/include"
export LDFLAGS="-L$OD_SYSROOT/usr/lib -Wl,-rpath-link,$OD_SYSROOT/usr/lib -Wl,--gc-sections"
export PKG_CONFIG=$SDK/bin/pkgconf
export PKG_CONFIG_SYSROOT_DIR=$OD_SYSROOT
export PKG_CONFIG_LIBDIR=$OD_SYSROOT/usr/lib/pkgconfig:$OD_SYSROOT/usr/share/pkgconfig
export PKG_CONFIG_PATH=
JOBS=${OD_JOBS:-1}
if [ "$JOBS" != 1 ]; then echo 'Native build scope requires OD_JOBS=1' >&2; exit 2; fi
mkdir -p "$ROOT/engine-logs" "$ROOT/engine-build" "$ROOT/engine-downloads" "$ROOT/built-native"
if [ "${1:-inspect}" != inspect ]; then
    exec 9>"$ROOT/.native-build.lock"
    flock -n 9 || { echo 'Another native heavy phase is active; retry after it completes.' >&2; exit 2; }
    exec > >(tee -a "$ROOT/engine-logs/${1}.log") 2>&1
fi

cm() {
    cmake -DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain.cmake" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" \
        -DCMAKE_INSTALL_PREFIX=/usr "$@"
}
fetch() {
    local url=$1 name=$2
    if [ ! -f "$ROOT/engine-downloads/$name" ]; then
        curl --fail --location --retry 3 "$url" -o "$ROOT/engine-downloads/$name.part"
        mv "$ROOT/engine-downloads/$name.part" "$ROOT/engine-downloads/$name"
    fi
    sha256sum "$ROOT/engine-downloads/$name"
}
unpack() {
    local name=$1 dir=$2
    if [ ! -d "$ROOT/sources/$dir" ]; then
        mkdir -p "$ROOT/sources/$dir"
        tar -xf "$ROOT/engine-downloads/$name" -C "$ROOT/sources/$dir" --strip-components=1
    fi
}
auto() {
    local name=$1
    shift
    mkdir -p "$ROOT/engine-build/$name"
    cd "$ROOT/engine-build/$name"
    if [ ! -f Makefile ]; then
        "$ROOT/sources/$name/configure" --host=mipsel-buildroot-linux-musl --prefix=/usr \
            --disable-static --enable-shared "$@"
    fi
    make -j"$JOBS"
    make DESTDIR="$OD_SYSROOT" install
    # SDK .la metadata contains host-looking /usr/lib paths during relinking.
    # Only discard generated metadata in our isolated copy, not SDK libraries.
    find "$OD_SYSROOT/usr/lib" -maxdepth 1 -name '*.la' -delete
}
stage() {
    local name=$1 binary=$2
    local destination=${3:-$ROOT/built-native}
    mkdir -p "$destination/bin" "$destination/licenses/$name"
    cp "$binary" "$destination/bin/$name"
    "$STRIP" "$destination/bin/$name"
    "$SDK/bin/mipsel-buildroot-linux-musl-readelf" -h -A -d "$destination/bin/$name" \
        > "$destination/$name.abi"
    sha256sum "$destination/bin/$name"
}
native_patch() {
    local name=$1 patch=$PROJECT/package/lilsmoke-od-engines/patches/$1-native.patch
    if git -C "$ROOT/sources/$name" apply --reverse --check --recount "$patch" 2>/dev/null; then
        return
    fi
    git -C "$ROOT/sources/$name" apply --check --recount "$patch"
    git -C "$ROOT/sources/$name" apply --recount "$patch"
}
complete_nx_patch() {
    local patch
    for patch in "$PROJECT"/package/lilsmoke-od-engines/patches/nxengine-{complete,framebuffer}.patch; do
        if git -C "$ROOT/sources/nxengine" apply --reverse --check --recount "$patch" 2>/dev/null; then continue; fi
        git -C "$ROOT/sources/nxengine" apply --check --recount "$patch"
        git -C "$ROOT/sources/nxengine" apply --recount "$patch"
    done
}
MENU_FLAGS="-I$PROJECT/package/lilsmoke-od-engines/src -I$ROOT/sources/prboom/prboom2/src/TEXTSCREEN"

case "${1:-inspect}" in
prepare)
    python3 "$HERE/prepare.py" "$ROOT" "$HERE/sources.json"
    ;;
inspect)
    for name in nxengine prboom easyrpg scummvm openbor; do
        printf '\n%s\n' "$name"
        git -C "$ROOT/sources/$name" log -1 --format='%H %D %s'
        git -C "$ROOT/sources/$name" status --short
    done
    find "$ROOT" -maxdepth 2 -type f -name '*.log' -printf '%s %p\n'
    ls "$STAGING/usr/lib" | grep -E 'SDL|lcf|fmt|pixman|vorbis|ogg|expat'
    ;;
shim)
    mkdir -p "$ROOT/adapter"
    env CPPFLAGS= make -C "$ROOT/adapter" -f "$PROJECT/package/lilsmoke-od-sdl/Makefile" \
        -j"$JOBS" PROJECT="$PROJECT" CC="$CC" CXX="$CXX" \
        CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" \
        SDL2_INCLUDE="$STAGING/usr/include/SDL2" SDL12_INCLUDE="$STAGING/usr/include/SDL"
    cp "$ROOT/adapter/libod-sdl.so" "$ROOT/built-native/libod-sdl.so"
    "$STRIP" "$ROOT/built-native/libod-sdl.so"
    "$SDK/bin/mipsel-buildroot-linux-musl-readelf" -h -A -d "$ROOT/built-native/libod-sdl.so" \
        > "$ROOT/built-native/libod-sdl.so.abi"
    ;;
deps)
    if [ ! -f "$OD_SYSROOT/.sdk-copied" ]; then
        mkdir -p "$OD_SYSROOT"
        cp -a --reflink=auto "$STAGING/." "$OD_SYSROOT/"
        touch "$OD_SYSROOT/.sdk-copied"
    fi
    find "$OD_SYSROOT/usr/lib" -maxdepth 1 -name '*.la' -delete
    fetch https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz libogg-1.3.5.tar.xz
    unpack libogg-1.3.5.tar.xz ogg
    auto ogg
    fetch https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz libvorbis-1.3.7.tar.xz
    unpack libvorbis-1.3.7.tar.xz vorbis
    cm -S "$ROOT/sources/vorbis" -B "$ROOT/engine-build/vorbis-cmake" \
        -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF
    cmake --build "$ROOT/engine-build/vorbis-cmake" -j"$JOBS"
    DESTDIR="$OD_SYSROOT" cmake --install "$ROOT/engine-build/vorbis-cmake"
    fetch https://www.libsdl.org/projects/SDL_mixer/release/SDL2_mixer-2.6.3.tar.gz SDL2_mixer-2.6.3.tar.gz
    unpack SDL2_mixer-2.6.3.tar.gz sdl2-mixer
    auto sdl2-mixer --with-sdl-prefix="$OD_SYSROOT/usr" --disable-sdltest \
        --disable-music-cmd --disable-music-mod --disable-music-midi --disable-music-flac \
        --disable-music-mp3 --disable-music-opus --disable-music-ogg-shared
    fetch https://xorg.freedesktop.org/releases/individual/lib/pixman-0.42.2.tar.xz pixman-0.42.2.tar.xz
    unpack pixman-0.42.2.tar.xz pixman
    auto pixman --disable-gtk --disable-loongson-mmi --disable-arm-iwmmxt \
        --disable-arm-simd --disable-arm-neon --disable-vmx --disable-sse2 --disable-ssse3
    fetch https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.tar.gz fmt-10.2.1.tar.gz
    unpack fmt-10.2.1.tar.gz fmt
    cm -S "$ROOT/sources/fmt" -B "$ROOT/engine-build/fmt" -DFMT_TEST=OFF -DBUILD_SHARED_LIBS=OFF
    cmake --build "$ROOT/engine-build/fmt" -j"$JOBS"
    DESTDIR="$OD_SYSROOT" cmake --install "$ROOT/engine-build/fmt"
    fetch https://easyrpg.org/downloads/player/0.8.1/liblcf-0.8.1.tar.gz liblcf-0.8.1.tar.gz
    unpack liblcf-0.8.1.tar.gz liblcf
    fetch https://github.com/benhoyt/inih/archive/refs/tags/r58.tar.gz inih-58.tar.gz
    unpack inih-58.tar.gz inih
    "$CC" $CFLAGS -fPIC -c "$ROOT/sources/inih/ini.c" -o "$ROOT/engine-build/inih.o"
    "$AR" rcs "$OD_SYSROOT/usr/lib/libinih.a" "$ROOT/engine-build/inih.o"
    cp "$ROOT/sources/inih/ini.h" "$OD_SYSROOT/usr/include/"
    cm -S "$ROOT/sources/liblcf" -B "$ROOT/engine-build/liblcf" \
        -DCMAKE_PROJECT_INCLUDE="$HERE/liblcf-options.cmake" \
        -DBUILD_SHARED_LIBS=OFF -DLIBLCF_WITH_ICU=OFF -DLIBLCF_WITH_XML=OFF -DLIBLCF_ENABLE_TESTS=OFF
    cmake --build "$ROOT/engine-build/liblcf" -j"$JOBS"
    DESTDIR="$OD_SYSROOT" cmake --install "$ROOT/engine-build/liblcf"
    ;;
liblcf)
    cm -S "$ROOT/sources/liblcf" -B "$ROOT/engine-build/liblcf" \
        -DCMAKE_PROJECT_INCLUDE="$HERE/liblcf-options.cmake" \
        -DBUILD_SHARED_LIBS=OFF -DLIBLCF_WITH_ICU=OFF -DLIBLCF_WITH_XML=OFF -DLIBLCF_ENABLE_TESTS=OFF
    cmake --build "$ROOT/engine-build/liblcf" -j1
    DESTDIR="$OD_SYSROOT" cmake --install "$ROOT/engine-build/liblcf"
    ;;
nxengine)
    if ! grep -q 'nx-import.h' "$ROOT/sources/nxengine/src/main.cpp"; then native_patch nxengine; fi
    complete_nx_patch
    patch=$PROJECT/package/lilsmoke-od-engines/patches/nxengine-native-resolution.patch
    if ! git -C "$ROOT/sources/nxengine" apply --reverse --check --recount "$patch" 2>/dev/null; then
        git -C "$ROOT/sources/nxengine" apply --recount "$patch"
    fi
    cm -S "$ROOT/sources/nxengine" -B "$ROOT/engine-build/nxengine" -DPORTABLE=OFF \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
        -DCMAKE_CXX_FLAGS="$CXXFLAGS $MENU_FLAGS" \
        -DCMAKE_INSTALL_DATADIR=/storage/emulators/opendingux/data/share
    cmake --build "$ROOT/engine-build/nxengine" --target nx extract -j"$JOBS"
    stage nxengine "$ROOT/engine-build/nxengine/nxengine-evo"
    stage nxextract "$ROOT/engine-build/nxengine/nxextract"
    cp "$ROOT/sources/nxengine/LICENSE" "$ROOT/built-native/licenses/nxengine/"
    cp "$ROOT/sources/nxengine/LICENSE" "$ROOT/built-native/licenses/nxextract/"
    mkdir -p "$ROOT/built-native/data/share/nxengine/data"
    find "$ROOT/sources/nxengine/data" -maxdepth 1 -type f \
        \( -name '*.fnt' -o -name '*.json' -o -name '*.sif' -o -name '*.dat' -o -name 'font_*.png' -o -name 'spot.png' \) \
        -exec cp {} "$ROOT/built-native/data/share/nxengine/data/" \;
    ;;
prboom)
    native_patch prboom
    env CC=/usr/bin/cc CXX=/usr/bin/c++ CFLAGS= CXXFLAGS= CPPFLAGS= LDFLAGS= \
        PKG_CONFIG=/usr/bin/pkg-config PKG_CONFIG_SYSROOT_DIR= PKG_CONFIG_LIBDIR= \
        cmake -S "$ROOT/sources/prboom/prboom2" -B "$ROOT/engine-build/prboom-host" \
        -DBUILD_GL=OFF -DWITH_MIXER=OFF -DWITH_NET=OFF -DWITH_ALSA=OFF \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/engine-build/prboom-host" --target rdatawad -j"$JOBS"
    cm -S "$ROOT/sources/prboom/prboom2" -B "$ROOT/engine-build/prboom" \
        -DCMAKE_C_FLAGS="$CFLAGS $MENU_FLAGS -G0 -fno-data-sections" \
        -DIMPORT_EXECUTABLES="$ROOT/engine-build/prboom-host/ImportExecutables.cmake" \
        -DBUILD_GL=OFF -DWITH_IMAGE=OFF -DWITH_MIXER=ON -DWITH_NET=OFF -DWITH_PCRE=OFF \
        -DWITH_MAD=OFF -DWITH_FLUIDSYNTH=OFF -DWITH_DUMB=OFF -DWITH_VORBISFILE=OFF \
        -DWITH_PORTMIDI=OFF -DWITH_ALSA=OFF \
        -DPRBOOMDATADIR=/storage/emulators/opendingux/data/share/prboom \
        -DDOOMWADDIR=/storage/roms/doom
    cmake --build "$ROOT/engine-build/prboom" -j"$JOBS"
    stage prboom "$ROOT/engine-build/prboom/prboom-plus"
    mkdir -p "$ROOT/built-native/data/share/prboom"
    cp "$ROOT/engine-build/prboom/prboom-plus.wad" "$ROOT/built-native/data/share/prboom/"
    cp "$ROOT/sources/prboom/prboom2/COPYING" "$ROOT/built-native/licenses/prboom/"
    ;;
easyrpg)
    if grep -q 'Color(0, 0, 0));' "$ROOT/sources/easyrpg/src/scene.cpp"; then
        git -C "$ROOT/sources/easyrpg" apply --recount "$PROJECT/package/lilsmoke-od-engines/patches/easyrpg-browser-alpha.patch"
    fi
    patch=$PROJECT/package/lilsmoke-od-engines/patches/easyrpg-browser-exit.patch
    if ! git -C "$ROOT/sources/easyrpg" apply --reverse --check --recount "$patch" 2>/dev/null; then
        git -C "$ROOT/sources/easyrpg" apply --recount "$patch"
    fi
    cm -S "$ROOT/sources/ogg" -B "$ROOT/engine-build/ogg-cmake" -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF
    cmake --build "$ROOT/engine-build/ogg-cmake" -j1
    DESTDIR="$OD_SYSROOT" cmake --install "$ROOT/engine-build/ogg-cmake"
    cm -S "$ROOT/sources/easyrpg" -B "$ROOT/engine-build/easyrpg" \
        -DPLAYER_TARGET_PLATFORM=SDL2 -DPLAYER_WITH_HARFBUZZ=OFF -DPLAYER_WITH_LHASA=OFF \
        -DPLAYER_WITH_NLOHMANN_JSON=OFF -DPLAYER_WITH_NATIVE_MIDI=OFF \
        -DPLAYER_WITH_MPG123=OFF -DPLAYER_WITH_LIBSNDFILE=OFF -DPLAYER_WITH_OGGVORBIS=ON \
        -DPLAYER_WITH_OPUS=OFF -DPLAYER_WITH_WILDMIDI=OFF -DPLAYER_WITH_FLUIDSYNTH=OFF \
        -DPLAYER_WITH_FLUIDLITE=OFF -DPLAYER_WITH_XMP=OFF -DPLAYER_AUDIO_RESAMPLER=OFF \
        -DPLAYER_ENABLE_TESTS=OFF
    cmake --build "$ROOT/engine-build/easyrpg" -j"$JOBS"
    stage easyrpg "$ROOT/engine-build/easyrpg/easyrpg-player"
    cp "$ROOT/sources/easyrpg/COPYING" "$ROOT/built-native/licenses/easyrpg/"
    ;;
scummvm)
    cd "$ROOT/sources/scummvm"
    if [ ! -f config.mk ] || [ "${OD_RECONFIGURE:-0}" = 1 ]; then
        LDFLAGS="${LDFLAGS/ -Wl,--gc-sections/}" \
        CPPFLAGS="$CPPFLAGS -I$OD_SYSROOT/usr/include/directfb" \
        ./configure --host=mipsel-buildroot-linux-musl --backend=sdl --with-sdl-prefix="$OD_SYSROOT/usr" \
            --disable-all-engines --enable-engine=scumm,agi,sci,agos,kyra,queen,sky,lure,sword1,sword2,drascula \
            --opengl-mode=none --disable-opengl-game --disable-cloud --disable-libcurl \
            --disable-fluidsynth --disable-mt32emu --disable-mad --disable-fribidi --disable-theoradec \
            --disable-flac --disable-alsa --disable-a52 --disable-mpeg2 --disable-faad --disable-nasm \
            --disable-taskbar --disable-sdlnet --disable-discord --disable-tts --disable-updates \
            --disable-detection-full --disable-hq-scalers --disable-enet --disable-system-dialogs \
            --disable-bink --disable-vpx --disable-lua --disable-fluidlite --disable-sonivox \
            --enable-release --datadir=/storage/emulators/opendingux/data/share/scummvm
    fi
    make -j"$JOBS"
    stage scummvm "$ROOT/sources/scummvm/scummvm"
    mkdir -p "$ROOT/built-native/data/share/scummvm"
    cp "$ROOT/sources/scummvm/gui/themes/scummclassic.zip" "$ROOT/built-native/data/share/scummvm/"
    for file in gui-icons.dat translations.dat; do
        cp "$ROOT/sources/scummvm/gui/themes/$file" "$ROOT/built-native/data/share/scummvm/"
    done
    for file in kyra.dat lure.dat drascula.dat queen.tbl sky.cpt fonts.dat macgui.dat encoding.dat classicmacfonts.dat; do
        cp "$ROOT/sources/scummvm/dists/engine-data/$file" "$ROOT/built-native/data/share/scummvm/"
    done
    cp "$ROOT/sources/scummvm/COPYING" "$ROOT/sources/scummvm/COPYRIGHT" "$ROOT/built-native/licenses/scummvm/"
    ;;
mgba)
    sh "$HERE/build-mgba-native.sh"
    ;;
openbor)
    sh "$HERE/build-sdl-gfx.sh"
    sh "$HERE/build-openbor.sh"
    stage openbor "$ROOT/native-candidates/bin/openbor"
    cp "$ROOT/native-candidates/licenses/openbor/"* "$ROOT/built-native/licenses/openbor/"
    ;;
mame4all)
    sh "$HERE/build-mame4all.sh"
    stage mame4all "$ROOT/native-candidates/bin/mame4all"
    stage mame4all-engine "$ROOT/native-candidates/bin/mame4all-engine"
    cp -a "$ROOT/native-candidates/data/share/mame4all" "$ROOT/built-native/data/share/"
    cp "$ROOT/native-candidates/licenses/mame4all/"* "$ROOT/built-native/licenses/mame4all/"
    ;;
easyrpg-core|scummvm-core)
    sh "$HERE/build-$1.sh"
    ;;
*) echo 'usage: build.sh prepare|inspect|shim|deps|liblcf|nxengine|prboom|easyrpg|scummvm' >&2; exit 2;;
esac
