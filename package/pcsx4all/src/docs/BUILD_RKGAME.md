# Build notes for the rkgame port

## Required host

Use Linux or an SDK shell that provides:

- GNU make
- `mipsel-linux-gcc`
- `mipsel-linux-g++`
- `mipsel-linux-ar`
- SDL 1.2 headers and libraries in the cross sysroot
- zlib in the cross sysroot

The original binary reports:

```text
GCC: (Codescape GNU Tools 2018.09-02 for MIPS MTI Linux) 6.3.0
```

That toolchain family is preferred for close binary comparison.

## Default build

```sh
make -f Makefile.rkgame clean
make -f Makefile.rkgame V=1
```

Output:

```text
libpcsx4all_rkgame.a
```

## Local Windows validation build

When a complete vendor MIPS SDK is not available, the tree can still be
compiled locally with Zig as a MIPS compiler:

```sh
make -f Makefile.rkgame ZIG_MIPS=1 clean
make -f Makefile.rkgame ZIG_MIPS=1 V=1
```

If `zig` is not on `PATH`, pass it explicitly:

```sh
make -f Makefile.rkgame ZIG_MIPS=1 ZIG=/path/to/zig V=1
```

This mode targets `mipsel-linux-musl`, disables SDL-specific objects, and adds
`src/port/rkgame/compat` only for compile-time compatibility. It is intended to
prove that the reconstructed PCSX/MIPS core builds into a valid object archive;
the final firmware link should still use the device SDK and real libraries.

Validated local output:

```text
libpcsx4all_rkgame.a
SHA256: 950B2079BD5EEC9E295FBC851CC4599A51380F7F5266F3AA40FBED43C75372F6
Archive members: 38
Representative objects: ELF32 LSB relocatable, MIPS32r2, o32, hard-float
```

## Important build choices

`Makefile.rkgame` intentionally keeps these options from the matched upstream
and target binary:

- `GPU = gpu_unai`
- `SPU = spu_pcsxrearmed`
- `RECOMPILER = mips`
- `USE_GPULIB = 1`
- `MMAP_TO_ADDRESS_ZERO`
- `-Wl,-Ttext-segment=0x40000000` for executable-style firmware linkage

The static archive target does not link an executable by itself. The firmware
owner should link it into the final program and provide any stronger symbols
that override weak defaults, such as `work_path`, logging, memory setup, or
video/input callbacks.

## Runtime layout

The port prefers:

```text
/mnt/sdcard/cubegm/cores/.pcsx4all/
/mnt/sdcard/cubegm/cores/.pcsx4all/memcards/
/mnt/sdcard/cubegm/cores/.pcsx4all/bios/
/mnt/sdcard/cubegm/cores/.pcsx4all/patches/
/mnt/sdcard/cubegm/cores/.pcsx4all/sstates/
```

If that folder does not exist, it falls back to `$HOME/.pcsx4all`.

## Suggested comparison after build

Compare the rebuilt firmware artifact against the original `rkgame`:

```sh
file rkgame rebuilt-rkgame
strings rebuilt-rkgame | grep -E "STv4 PCSX|spu_pcsxrearmed|pcsx4all"
readelf -h rebuilt-rkgame
readelf -Ws rebuilt-rkgame | grep -E "PCSX_Load|pcsx_load_game|pcsx_save_state"
```

For deeper comparison, inspect the rebuilt disassembly around:

- `psxInit`
- `LoadPlugins`
- `SPU_init`
- `rec_mmap_psx_mem`
- `pcsx_load_game`
