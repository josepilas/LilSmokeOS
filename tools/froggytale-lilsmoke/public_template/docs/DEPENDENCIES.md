# Offline Build Dependencies

Required external system tools: compatible GCC 12.3 MIPS32r2/o32 hard-float
musl SDK (compiler, binutils, C library/sysroot), Python 3.9+, Linux taskset,
nice and prlimit. Host make and CMake or configure tools are needed only when
rebuilding the bundled dependency libraries. No SDK is copied into this repo.

The normal `build.py` links the compatible SDL2/libatomic already installed in
your supplied SDK, using the exact supplied SDL2 public build headers. It
verifies the pinned FroggyTale Git-blob inventory and every staged source unit
without fetching. `--verify-only` proves source/staging identity without an
engine rebuild. The public release keeps the previously qualified binary.

For dependency configure/make commands, put the SDK's cross binutils on PATH
so configure does not fall back to host ar/nm/strip/ranlib:

```sh
export PATH="$SDK/host/bin:$PATH"
export CFLAGS="-O2 -mips32r2 -mabi=32 -mfp32 -mhard-float"
```

## SDL2 2.28.5

`deps/SDL2/` includes src, include, cmake, acinclude, build-scripts and top-level
build/configure/license files. `deps/SDL2-build-headers/` preserves the actual
public headers consumed by the qualified engine. Build out of tree, serially,
using a recipient-supplied CMake cross toolchain or configure environment:

```sh
mkdir /path/to/sdl-build
cd /path/to/sdl-build
CC="$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" \
  /path/to/checkout/deps/SDL2/configure \
  --host=mipsel-buildroot-linux-musl --prefix=/usr \
  --disable-video-opengl --disable-video-opengles --disable-video-vulkan \
  --disable-static --enable-shared
make -j1
make DESTDIR=/path/to/dependency-staging install
```

This is a compatible dependency rebuild, not a claim of byte-identical SDL
output on any host. The original configure selections and generated config
header are recorded in `evidence/dependency-build.json`. Match those selections
and the SDK when exact backend behavior is required. No GPU backend is needed.

## GCC 12.3 libatomic

The `deps/gcc-12.3.0/` subset includes libatomic, GCC config/include support,
top-level m4/configure-helper files, version files and license texts. It is not
the GCC compiler source tree and must not be used to claim a complete compiler
redistribution. The target runtime can be configured independently:

```sh
mkdir /path/to/atomic-build
cd /path/to/atomic-build
CC="$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" \
  /path/to/checkout/deps/gcc-12.3.0/libatomic/configure \
  --host=mipsel-buildroot-linux-musl --prefix=/usr \
  --disable-multilib --disable-static --enable-shared
make -j1
make DESTDIR=/path/to/dependency-staging install
```

Use the original MIPS32r2/o32 hard-float flags from `build-plan.json`. The
release's SDL2/libatomic ELF identities are in `release-lock.json`. Do not
replace the qualified engine or libraries silently when repackaging 0.0.1.

The optional upstream Python atlas packer requires Pillow on the host; it is
not used by the engine build or on-device converter. No external game asset
or atlas is needed to compile the runtime or run its authored synthetic tests.
