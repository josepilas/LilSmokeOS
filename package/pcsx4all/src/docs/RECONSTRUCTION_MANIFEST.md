# Reconstruction manifest

## Added files

- `Makefile.rkgame`
- `README_RKGAME_RECONSTRUCTION.md`
- `docs/BUILD_RKGAME.md`
- `docs/FIDELITY_NOTES.md`
- `docs/RECONSTRUCTION_MANIFEST.md`
- `docs/key_function_renames.tsv`
- `docs/source_match_evidence.tsv`
- `src/port/rkgame/compat/zlib.h`
- `src/port/rkgame/port.cpp`
- `src/port/rkgame/port.h`
- `src/port/rkgame/rkgame_bridge.h`

## Modified upstream files

- `src/r3000a.cpp`
- `src/misc.cpp`

The `r3000a.cpp` change is guarded by `RKGAME_FIRMWARE` and
`RKGAME_PCSX_BUILD_DATE`, so normal upstream-style builds keep their original
date behavior.

The `misc.cpp` change adds the missing Unix declaration header for
`dup`, `close`, and `fsync`, which modern MIPS compilers require.

## Main reconstructed behavior

- Firmware-facing `PCSX_Load` thread wrapper.
- Firmware-facing `pcsx_load_game` startup path.
- Runtime path layout under `cores/.pcsx4all`.
- `pcsx4all.cfg` parser with `clip_368`, `AnalogArrow`, and `Analog_Mode`.
- Savestate wrappers that mirror the decompiled path mutation pattern.
- GPU UNAI defaults matching the firmware profile.
- SPU `spu_pcsxrearmed` defaults matching the firmware profile.
- Optional framebuffer, input, and logging callbacks for firmware integration.
- Local `ZIG_MIPS=1` validation path that emits a MIPS32r2/o32 hard-float
  static archive.

## Not included

- A byte-identical recreation of the vendor `rkgame` executable.
- The private complete firmware frontend source.
- A complete vendor SDK link/runtime test on the physical device.
