# Fidelity notes

## High-confidence matches

- PCSX4ALL version `1.9`.
- MIPS-to-MIPS dynarec.
- `spu_pcsxrearmed`.
- GPU UNAI/gpulib.
- Runtime path pattern `%scores/.pcsx4all`, with `work_path` expected to end in
  a slash.
- `PCSX_Load`, `pcsx_Thread`, `pcsx_load_game`, `pcsx_save_state`, and
  `pcsx_load_state` firmware-facing entry points.
- Fixed build-date string for this firmware profile: `Feb 14 2024`.
- Config key `clip_368`, mapped to upstream `gpu_unai_config_ext.pixel_skip`.

## Source-level reconstruction choices

The binary contains frontend/private firmware calls that were not available as
source. The reconstruction therefore exposes clean callbacks for:

- frame output
- controller input
- logging
- work-path setup

When the real firmware provides equivalent functions, link them over these
callbacks or replace the weak/default glue in `src/port/rkgame/port.cpp`.

## Important differences from raw decompiler output

The decompiler output was not used as buildable C. It was used to recover
entry-point behavior, defaults, strings, and firmware glue. The buildable base
is the public PCSX4ALL source that matched the binary.

## Known incomplete areas

- The exact private video blitter ABI from `rkgame` is not fully recovered.
- The exact vendor input mapping may differ from the SDL fallback.
- The local Windows validation build produces the reconstructed PCSX/MIPS core
  archive, not a fully linked vendor `rkgame` executable.
- `RKGAME_MATCH_THREAD_DETACH` can be defined if a byte-behavior experiment
  needs the decompiled `pthread_detach` pattern, but the default source avoids
  `join + detach` because it is not robust across pthread implementations.
- Byte-identical output is unlikely without the original complete firmware
  source tree and exact build system.
