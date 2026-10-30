# PCSX4ALL rkgame reconstruction

This tree is a source-level reconstruction of the PS1/PCSX portion found in:

`C:/path/to/local-input/rkgame`

It keeps the public PCSX4ALL 1.9 source lineage as the buildable base and adds
a firmware-oriented `rkgame` port reconstructed from the MIPS decompilation.

## What changed from upstream

- Added `Makefile.rkgame`.
- Added `src/port/rkgame/port.cpp`.
- Added `src/port/rkgame/port.h`.
- Added `src/port/rkgame/rkgame_bridge.h`.
- Added `src/port/rkgame/compat/zlib.h` for local `ZIG_MIPS=1`
  compile-only validation when a full firmware sysroot is not present.
- Added `RKGAME_FIRMWARE` logging/date handling in `src/r3000a.cpp`.
- Added a missing Unix include in `src/misc.cpp` for modern MIPS compilers.
- Added documentation and evidence tables under `docs/`.

The original `port/sdl` frontend was not removed. The `rkgame` port is isolated
so the upstream SDL build remains comparable.

## Target profile

- Architecture: MIPS little-endian Linux.
- Toolchain family: `mipsel-linux-*`.
- CPU core: MIPS-to-MIPS dynarec.
- GPU: `gpu_unai` through gpulib.
- SPU: `spu_pcsxrearmed`.
- Runtime folder: `/mnt/sdcard/cubegm/cores/.pcsx4all`.
- Config file: `pcsx4all.cfg`.
- BIOS default: `scph1001.bin`.

## Firmware exports

The reconstructed port exports:

- `PCSX_Load(const char *game_path)`
- `pcsx_Thread(void *unused)`
- `pcsx_load_game(const char **game_path)`
- `pcsx_save_state(char *path)`
- `pcsx_load_state(char *path)`
- `retro_save_state(char *path)`
- `retro_load_state(char *path)`

It also exposes optional integration helpers:

- `rkgame_pcsx_set_work_path`
- `rkgame_pcsx_set_video_buffer`
- `rkgame_pcsx_set_video_callback`
- `rkgame_pcsx_set_input_callback`
- `rkgame_pcsx_set_log_callback`

## Build

On a Linux/MIPS cross-build environment with SDL 1.2 development files:

```sh
make -f Makefile.rkgame clean
make -f Makefile.rkgame
```

This produces:

```text
libpcsx4all_rkgame.a
```

That archive is meant to be linked into a firmware executable similar to
`rkgame`. There is also an experimental shared-library target:

```sh
make -f Makefile.rkgame shared
```

The original firmware binary was an ELF executable with the PCSX code embedded,
so the static archive is the more faithful reconstruction target.

This package was also locally cross-compiled on Windows with Zig 0.16.0:

```sh
make -f Makefile.rkgame ZIG_MIPS=1 clean
make -f Makefile.rkgame ZIG_MIPS=1 V=1
```

That mode disables SDL linkage and uses `src/port/rkgame/compat/zlib.h` only as
a compile-time ABI declaration. The resulting archive still keeps zlib calls as
external symbols for the firmware or final SDK link.

## Validation status

This source tree has been organized and checked locally for structure, symbol
coverage, source/decompilation consistency, and MIPS compilation.

Local build artifact:

```text
libpcsx4all_rkgame.a
SHA256: 950B2079BD5EEC9E295FBC851CC4599A51380F7F5266F3AA40FBED43C75372F6
```

Representative object validation:

```text
ELF 32-bit LSB relocatable, MIPS, MIPS32 rel2
Flags: noreorder, cpic, o32, mips32r2
FP ABI: Hard float (double precision)
```

The next real firmware validation step is to link the archive with the vendor
frontend/SDK libraries and run it on-device. For closer binary comparison, use
Codescape GNU Tools 2018.09-02 / GCC 6.3.0 or a compatible `mipsel-linux-gcc`.

## Evidence

- `docs/key_function_renames.tsv`
- `docs/source_match_evidence.tsv`

These files link the reconstructed source-level decisions to the decompiled
functions and public PCSX4ALL source matches.
