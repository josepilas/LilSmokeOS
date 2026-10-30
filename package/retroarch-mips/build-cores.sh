#!/bin/sh
# Pinned source builds. No architecture-ambiguous prebuilt fallback.
set -eu
[ "$#" -ge 3 ] || { echo "usage: build-cores.sh work-dir stage-dir core..." >&2; exit 2; }
package=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
lock=${LILSMOKE_CORE_LOCK:-$package/cores.lock}
work=$1
stage=$2
shift 2
: "${CC:?}" "${CXX:?}" "${AR:?}" "${RANLIB:?}" "${STRIP:?}" "${READELF:?}"
export GIT_TERMINAL_PROMPT=0
mkdir -p "$work" "$stage"
work=$(CDPATH='' cd -- "$work" && pwd)
stage=$(CDPATH='' cd -- "$stage" && pwd)
machine=$("$CC" -dumpmachine)
case "$machine" in mipsel-*) arch=mips; mips=1 ;; *) arch=${machine%%-*}; mips=0 ;; esac
for wanted do
    build_platform=unix
    set --
    if [ "$wanted" = pcsx_rearmed ]; then
        # No ARM dynarec/NEON or speculative fixed mapping on HC16xx. Single
        # compiler/render/SPU threads keep the 110 MiB target predictable.
        set -- DYNAREC=lightrec BUILTIN_GPU=unai LIGHTREC_CUSTOM_MAP=0 \
            LIGHTREC_ENABLE_THREADED_COMPILER=0 LIGHTREC_ENABLE_DISASSEMBLER=0 \
            USE_ASYNC_CDROM=0 USE_ASYNC_GPU=0 USE_ASYNC_SPU=0 NDRC_THREAD=0 \
            HAVE_CHD=0 HAVE_PHYSICAL_CDROM=0 GIT_VERSION1= GIT_VERSION= 
    fi
    if [ "$mips" = 1 ] && [ "$wanted" = gpsp ]; then build_platform=mips32; fi
    entry=$(awk -F '|' -v name="$wanted" '$1 == name { print; n++ } END { if (n != 1) exit 1 }' "$lock") || {
        echo "Missing or duplicate core lock: $wanted" >&2; exit 2;
    }
    IFS='|' read -r name url ref mk built installed <<EOF
$entry
EOF
    case "$ref" in *[!0-9a-f]*|'') echo "Unpinned ref: $ref" >&2; exit 2 ;; esac
    [ "${#ref}" = 40 ] || exit 2
    src="$work/$name"
    if [ ! -d "$src/.git" ]; then
        if [ -d "$src" ] && [ -n "$(ls -A "$src")" ]; then
            echo "Refusing to replace non-git core sources: $src" >&2; exit 2
        fi
        git init -q "$src"
        git -C "$src" remote add origin "$url"
    fi
    [ "$(git -C "$src" remote get-url origin)" = "$url" ] || {
        echo "Core origin mismatch: $src" >&2; exit 2;
    }
    git -C "$src" diff --quiet
    git -C "$src" diff --cached --quiet
    if ! git -C "$src" cat-file -e "$ref^{commit}" 2>/dev/null; then
        git -C "$src" -c credential.helper= fetch --depth 1 origin "$ref"
    fi
    git -C "$src" checkout -q --detach "$ref"
    git -C "$src" -c credential.helper= submodule update --init --recursive --depth 1
    [ "$(git -C "$src" rev-parse HEAD)" = "$ref" ] || exit 2
    # Always clean this package's build outputs so changed flags cannot reuse
    # incompatible objects. Source modifications are checked above, not reset.
    if [ "$mk" = CMakeLists.txt ]; then
        alignment_flags=''
        if [ "$mips" = 1 ]; then alignment_flags='-UFAST_LSB_WORD_ACCESS -fno-strict-aliasing'; fi
        cmake -S "$src" -B "$src/lilsmoke-build" \
            -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$arch" \
            -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_AR="$AR" -DCMAKE_RANLIB="$RANLIB" \
            -DCMAKE_SYSROOT="$("$CC" --print-sysroot)" \
            -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
            -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
            -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
            -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-DNDEBUG \
            -DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG \
            -DCMAKE_C_FLAGS="${CFLAGS:-} $alignment_flags" \
            -DCMAKE_CXX_FLAGS="${CXXFLAGS:-${CFLAGS:-}} $alignment_flags" \
            -DCMAKE_SHARED_LINKER_FLAGS="${LDFLAGS:-}" \
            -DBUILD_LIBRETRO=ON -DSKIP_LIBRARY=ON -DDISABLE_DEPS=ON \
            -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF \
            -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DBUILD_HEADLESS=OFF \
            -DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF -DENABLE_SCRIPTING=OFF
        cmake --build "$src/lilsmoke-build" --clean-first --target mgba_libretro -j "${JOBS:-2}"
    else
    make_dir="$src/$(dirname "$mk")"
    make_file=$(basename "$mk")
    make -C "$make_dir" -f "$make_file" platform="$build_platform" ARCH="$arch" arch="$arch" "$@" clean
    make -C "$make_dir" -f "$make_file" -f "$package/core-flags.mk" \
        -j"${JOBS:-2}" platform="$build_platform" ARCH="$arch" arch="$arch" \
        CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
        LILSMOKE_CORE_MIPS="$mips" \
        LILSMOKE_CORE_CFLAGS="${CFLAGS:-}" LILSMOKE_CORE_CXXFLAGS="${CXXFLAGS:-${CFLAGS:-}}" \
        LILSMOKE_CORE_LDFLAGS="${LDFLAGS:-}" \
        "$@" all
    fi
    found=$(find "$src" -name "$built" -type f)
    if [ "$(printf '%s\n' "$found" | wc -l)" != 1 ] || [ ! -f "$found" ]; then
        echo "Expected exactly one $built from $name" >&2; exit 3;
    fi
    "$READELF" -h "$found" >"$stage/$installed.abi"
    if [ "$mips" = 1 ]; then
        grep -q 'ELF32' "$stage/$installed.abi"
        grep -q 'little endian' "$stage/$installed.abi"
        grep -q 'MIPS' "$stage/$installed.abi"
    else
        if grep -q 'MIPS' "$stage/$installed.abi"; then exit 3; fi
    fi
    install -m 0644 "$found" "$stage/$installed"
    "$STRIP" --strip-unneeded "$stage/$installed"
    {
        printf 'core=%s\nsource=%s\ncommit=%s\ntarget=%s\n' "$name" "$url" "$ref" "$machine"
        "$CC" --version | head -1
        printf 'CFLAGS=%s\nCXXFLAGS=%s\nLDFLAGS=%s\n' "${CFLAGS:-}" "${CXXFLAGS:-}" "${LDFLAGS:-}"
        sha256sum "$stage/$installed"
    } >"$stage/$installed.build-info"
    echo "CORE_BUILT name=$name target=$machine commit=$ref file=$installed"
done
