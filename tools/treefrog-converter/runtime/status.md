# Runtime Handoff Status

CLI is stable and implemented:

```
lilretro CORE [CONTENT] --profile /app/runtime/launch.json
lilretro CORE [CONTENT] --profile PROFILE --probe --frames 180 --diagnostics report.json
```

The browser's current schema is accepted exactly: schema, port_id, renderer
software, max_width/max_height, audio_latency_ms, input retropad,
save_directory, hardware_rendering false, probe_on_first_launch and core_options.
Optional ABI uses `mipsel-o32-hardfloat-glibc` or `mipsel-o32-hardfloat-musl`.
Paths are profile-directory-relative. Runtime writes runtime-capabilities.json
beside the profile, or the --diagnostics override. No original run.sh/PicoArch
is invoked. Environment requests are observed and unknown ones return false.

Current completed artifacts:

- `_build/musl/lilretro`: MIPS32r2 little-endian o32 FP32 hardfloat,
  musl interpreter, SDL2. `_build/musl/test-results.json` records 15 passing
  QEMU 74Kf API tests plus a skipped real SDL-device test (SDK lacks dummy).
- `_build/host/lilretro`: native x86-64 software SDL2; initial 16 tests passed,
  including SDL dummy software display and PCM callback.
- Public matching SF3000 GCC 6.3.0/compiler components and headers fetched
  selectively into `_build/sf3000-sdk/`, pinned to public repository commit
  06eb2b99c4627a1200ecce188a80e029b60ed33e with Git-blob/hash verification.
- Glibc build uses that compiler, matching glibc headers/SDL1 and startup
  objects. Last relocation gaps fixed: multilib include order, prefixed
  assembler/linker location, sysroot linker paths, and unused LTO plugin.
  Frontend and test-core builds have succeeded. `_build/glibc/build.json` and
  `_build/glibc/lilretro` exist. Original 135440-byte frontend SHA256:
  ab07a945b686ad66b91d2336a2a1418a167b2aa3c171618d808525018d8998f3.
  License attribution-only rebuild may change debug/source line metadata;
  use the current build.json/pack manifest for final exact hashes.

Device revision built separately: `_build/glibc-dispframe/lilretro`, 142432
bytes, SHA256 3f98f9ea060fe07925cc086efd46d3aa07257d0a28fcee1f4f19b3f829922d81.
It supports `TF_DRIVER=/stock/driver.so TF_PRESENT=dispframe` for original-pitch
RGB565 software frames and batched signed-16 stereo PCM. No firmware driver
is bundled or modified; no SDL display/audio initialization on the driver path.
Other software pixel formats convert into a reusable RGB565 buffer. This is
a normal OS board ABI, not Libretro GPU rendering. The first frozen frontend
remains unchanged. Revised pack pointer: `_build/runtime-dispframe.zip`.
Do not infer physical M8 scaling/performance or gameplay compatibility from
headless/API tests. Revised glibc QEMU 74Kf tests passed 17 cases including
RGB565/direct and XRGB8888/reusable-conversion board video plus nonzero PCM.
Each driver test delivered 4 frames and 4410 stereo PCM frames at 44100 Hz.
Real SDL framebuffer/audio-device testing is explicitly skipped in user mode.

Portable build commands on Linux/WSL (SDKs are external, read-only inputs):

```
python3 -B build.py --target host
python3 -B build.py --target musl --sdk "$MUSL_BUILDROOT_OUTPUT"
python3 -B build.py --target glibc --sdk "$GLIBC_SYSROOT" --cc "$MIPS_GCC"
python3 -B pack.py --require-both --musl-sysroot "$MUSL_SYSROOT" --glibc-sysroot "$GLIBC_SYSROOT"
```

Pack path is `_build/runtime.zip`. Archive paths are glibc/lilretro,
musl/lilretro, licenses/*, source/* and SOURCE-OFFER.txt. The manifest records
actual frontend ELF dependencies, hashes, build provenance and missing platform
closure. No downloaded SDK libraries, old PicoArch binary, private Data.rsdk or
firmware drivers are allowed into that ZIP by default.

Exact glibc fallback: the OS/platform helper must expose a matching
`/lib/ld.so.1`, `libSDL-1.2.so.0`, glibc libdl/libm/libpthread/libc, and the
core's libstdc++/libgcc closure in a glibc process namespace, then exec the NEW
app-local glibc/lilretro there. This frontend binary can be public with its
complete source; SDK runtime-library redistribution remains BLOCKED until
exact corresponding sources/build patches are identified. A public GitHub MIT
label on the SDK mirror does not relicense GNU/SDL components.

New independent sources are MPL-2.0, Copyright (c) 2026 Jose Pilas (José Pilas).
No PicoArch-derived code is used. libretro.h, jsmn and gamepad-state.h retain
their MIT terms. No full private OS UI source is included or required.

The historical results above are not physical-device qualification. The
standalone publication uses a separate path-sanitized pack, retains the frozen
input hashes, and does not overwrite any historical binary. See its manifest
and source/publication-provenance.json for exact distributed identities.
