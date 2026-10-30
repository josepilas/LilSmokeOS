# Independent Libretro Runtime Contract

FIRST-WORKING BINARIES PRESERVED for parent qualification: glibc/lilretro
SHA256 ab07a945b686ad66b91d2336a2a1418a167b2aa3c171618d808525018d8998f3
(135440 bytes); musl/lilretro SHA256
39ed48edf65d9bfd725ebce59fb187903a7350f90e4aeabfcc9a8a1dbc47dd2b
(167960 bytes). Do not replace them during the parent's qualification.
Pack pointer: `_build/runtime.zip`, manifest `_build/runtime-manifest.json`.
No SDK runtime libraries are redistributed. Exact glibc/SDL platform closure is
external and this pack is NOT self-contained.

REQUESTED BOARD-SINK REVISION: `_build/glibc-dispframe/lilretro` is a separate
142432-byte binary, SHA256
3f98f9ea060fe07925cc086efd46d3aa07257d0a28fcee1f4f19b3f829922d81.
The original `_build/glibc/lilretro` has not changed. Revised pack will be
`_build/runtime-dispframe.zip`, generated with `pack.py --variant=-dispframe`.
Set `TF_DRIVER=/stock/driver.so TF_PRESENT=dispframe` to use the OS-provided
board ABI for RGB565 video and batched signed-16 stereo PCM. RGB565 frames pass
directly with original pitch; XRGB8888/0RGB1555 convert through one reusable
RGB565 buffer. No SDL video/audio initialization occurs on this route. SDL1/2
software output remains the fallback when TF_DRIVER is absent. Driver failure
is explicit, not silently hidden. Never ship a firmware driver in the pack.
This 2D board scaler/presentation ABI is not Libretro GPU/HW_RENDER support.
Musl hosts explicitly reject TF_DRIVER rather than loading a glibc driver.
There is no physical M8 acceptance claim from these API/QA tests.

Probe video hashing is bounded to <=1024 sampled bytes on frames 1,60,180.
PCM hashes/energy are sampled at <=256 bytes per core frame. Diagnostic sink
metadata explicitly labels this sampling; counts remain full callback counts.

The author-requested license change from the initial provisional GPL notice
to MPL-2.0 affects source comments only. Frozen binaries have NOT been rebuilt;
build.json preserves the exact original compilation-input SHA256s, while the
pack manifest records the final provided source-file SHA256s. There is no
functional source/binary divergence in the FIRST build. The board-sink revision
is rebuilt from the current MPL-2.0 sources and has its own build-input hashes.
New code is original, not PicoArch-derived.

CLI (no original shell or PicoArch involved):

```
lilretro CORE [CONTENT] --profile /app/adapter-runtime.json
lilretro CORE [CONTENT] --profile PROFILE --probe --frames 180 --diagnostics report.json
```

Profiles use schema 1. All relative paths are relative to the profile's parent,
not the invoking shell. Suggested installed profile:

```json
{
  "schema": 1,
  "abi": "mipsel-o32-hardfloat-glibc",
  "max_width": 1024,
  "max_height": 768,
  "max_audio_frames": 8192,
  "audio_latency_ms": 64,
  "controller_port": 0,
  "system_directory": "system",
  "save_directory": "saves",
  "options": {}
}
```

Browser profile compatibility: `port_id` is metadata; `renderer: "software"`,
`input: "retropad"`, `hardware_rendering: false`, `core_options: {}` and
`probe_on_first_launch: true` are accepted. `core_options` and `options` are
aliases. `abi` is optional, but if supplied it must equal the runner ABI above.
Unrecognized keys fail explicitly. First launch records its first 180 frames
without interrupting gameplay, and writes `runtime-capabilities.json` alongside
the profile. `--diagnostics FILE` overrides that destination. `--probe` always
runs a bounded scan (default 180 frames) and exits. Use `--headless` only for
software callback QA without opening device video/audio.

The browser performs static export/import/ELF/dependency validation; the runtime
does actual core API introspection. Probe mode executes trusted native code; it
is NOT a sandbox. `--frames` bounds the number of retro_run calls, not execution
time inside a malicious or hanging core. A supervisor must enforce wall time
and memory limits. Hardware rendering is refused. Unsupported interfaces return
false; no invented VFS/audio callback/thread/GPU support is advertised.

Proposed static pack (runtime-owned artifacts only):

```
manifest.json
glibc/lilretro
glibc/lib/<reviewed SONAME libraries only>
musl/lilretro
musl/lib/<reviewed SONAME libraries only>
licenses/
source/
SOURCE-OFFER.txt
```

Manifest entries contain SHA256, byte count, ELF interpreter, needed SONAMEs,
exports/imports/version needs, ABI flags, source identity and redistribution
status. A core selects ONE libc family; a glibc core must never be dlopened into
musl. OS hardware drivers stay OS-provided board ABI dependencies. Missing or
unreviewed glibc/SDL closure is a blocking/manual platform prerequisite, not a
successful self-contained conversion. Do not copy private firmware drivers.

Input reads the existing `/run/lilsmoke-gamepads.state` ABI-1 transport
(`LILSMOKE_GAMEPAD_STATE` override) for digital buttons and six axes. Existing
MIT `gamepad-state.h` is copied locally and included unchanged. SDL handles
keyboard/native joystick events. The musl native/common Input adapter can run
out-of-process; never preload it into a glibc host. The legacy joy_key protocol
loses analog data and is not the new host's primary input interface.

Runtime probes produce actual supported environment command results, core info,
registered options/defaults/choices, software pixel formats, frame/audio counts,
save-state API availability, errors and unsupported requests. Capability checks
do not prove original game data or hardware performance. Private Data.rsdk is
never included in source, tests or runtime packs.
