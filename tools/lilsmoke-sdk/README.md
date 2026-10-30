<!-- SPDX-License-Identifier: MPL-2.0 -->
<!-- Copyright (c) 2026 José Pilas. -->
# LilSmoke Applications SDK 0.3.0-dev

Build an interactive software SDL2 application, preview it on Linux/WSL, and
package it for LilSmoke Applications. Includes shell, native C11 and C++17
starters, public source helpers and a Python 3.10+ standard-library CLI.

This is SOURCE ONLY. No toolchain/sysroot, OS/private source, runtime binaries,
emulator, games, downloads, installer, global SDK writes or card access.

## Graphical Quickstart

From this directory (the extracted ZIP contains `lilsmoke-sdk/`), on Linux/WSL:

```sh
python3 --version
python3 -B lilsmoke_sdk.py doctor --target host
cc --version
pkg-config --modversion sdl2
python3 lilsmoke_sdk.py new studio --template sdl2-c --id org.example.studio --name "SDK Studio" --app-version 0.3.0-dev --launcher "SDK Studio.sh"
python3 lilsmoke_sdk.py build studio --target host
python3 lilsmoke_sdk.py run studio
```

Host preview requires an existing C compiler, pkg-config and SDL2 development
headers/runtime, plus an X11/Wayland desktop or WSLg. On Debian/Ubuntu the usual
packages are `python3`, `build-essential`, `pkg-config`, `libsdl2-dev`; the SDK
does not install them. No MIPS toolchain or private OS source is needed for this
preview. Arrow keys navigate/adjust, Ctrl/Space/A activate, Enter opens Pause,
Esc goes back. Mouse, touch and SDL gamepads also work. Save and clean exit
persist settings in `studio/_build/host/userdata/studio.ini`.

To produce a DEVICE package, a matching cross SDK must be supplied separately:

```sh
# Tested LOCAL WSL Ubuntu-24.04 root path; replace on another machine.
export LILSMOKE_SDK_OUTPUT=/root/lilsmoke-os/build/buildroot/output
python3 -B lilsmoke_sdk.py doctor --target mips --sdk "$LILSMOKE_SDK_OUTPUT"
python3 lilsmoke_sdk.py build studio --target mips --sdk "$LILSMOKE_SDK_OUTPUT"
python3 -B lilsmoke_sdk.py check-runtime studio --base-root "$LILSMOKE_SDK_OUTPUT/target"
python3 lilsmoke_sdk.py validate studio
python3 lilsmoke_sdk.py package studio --output SDK-Studio-0.3.0-dev.lilpck
python3 lilsmoke_sdk.py validate SDK-Studio-0.3.0-dev.lilpck
```

`--sdk` expects a Linux-hosted MIPS32r2 little-endian O32 FP32 hard-float musl
Buildroot output ROOT with `host/bin/mipsel-buildroot-linux-musl-{gcc,g++,readelf}`,
the intact `staging/` sysroot/link libraries, `target/` musl/SDL2 runtime and
`build/sdl2-*/LICENSE.txt`. A generic ARM, glibc or arbitrary MIPS SDK will not
work. This source ZIP does not provide that SDK or a public download location.
The [portable guide](docs/lilsmoke-applications-sdk.md) lists exact paths,
DirectFB/device dependencies, licenses, rebuild steps and troubleshooting.

The SDL2 MIPS profile copies only matching SDL2 plus its license and the app.
The target OS must already provide matching musl, DirectFB/libdirect/libfusion,
modules and board adapters, including runtime-loaded dependencies. Nothing
automatically bundles a whole sysroot or qualifies a new board. Host preview
uses HOST SDL2 and never puts its ELF in the package; `validate studio` correctly
fails until the target build exists. Builds/packages refuse existing output.

## Other Templates And Commands

```sh
python3 lilsmoke_sdk.py --help
python3 lilsmoke_sdk.py new note --id org.example.note --name "My Note" --launcher "My Note.SH"
python3 lilsmoke_sdk.py validate note
python3 lilsmoke_sdk.py package note --output My-Note-0.1.0.lilpck
python3 lilsmoke_sdk.py new native --template native-cpp --id org.example.native --name "Native Example"
python3 lilsmoke_sdk.py build native --target mips --sdk "$LILSMOKE_SDK_OUTPUT"
```

Shell generation/validation/packaging also work on Windows with your installed
Python. The shell note starter needs the target's foreground keyboard at run
time. `native-c`/`native-cpp` are static diagnostic examples, not GUIs/emulators.
Only `payload/` is packaged. Exactly one root `.sh` is required, with any safe
filename including spaces/uppercase extension; metadata `entrypoint` does not
select it. Nested scripts are allowed. Scripts must have LF and `#!/bin/sh`.
`--runtime TREE` explicitly merges regular files under `runtime/`, never guesses
dependencies. Do not put user saves, credentials or host binaries in payload.

Build is serial, one CPU, nice 10, with 768 MiB compiler virtual-address and
180-second per-command limits. Tool commands return JSON; `run` returns app
output/status. Read the guide for the actual launch/Open Core/keyboard contract,
the SDL2 API (including capture BEFORE Present), package safety limits and gaps.

## Tests And Source Release

```sh
python3 -B -m unittest discover -s tests -p 'test_*.py'
python3 -B source_release.py --output _build/release/LilSmoke-Applications-SDK-0.3.0-dev-source-final.zip
taskset -c 0 nice -n 10 python3 -B tests/release_workflow.py --archive _build/release/LilSmoke-Applications-SDK-0.3.0-dev-source-final.zip --out _build/extracted-check-030 --sdk "$LILSMOKE_SDK_OUTPUT" --gui
```

The release builder uses an explicit source allowlist, checks every file and
relative Markdown link, embeds a SHA-256 manifest and refuses existing output.
No `_build/`, generated package/binary, VCS/private OS code or toolchain enters
the ZIP. The extraction workflow uses real CLI commands; optional MIPS tests use
`qemu-mipsel -cpu 74Kf`, not a full VM. Host GUI QA additionally requires existing
Xvfb/xdotool; [commands and qualification limits](docs/lilsmoke-applications-sdk.md#verification)
are in the guide. Repository-only `tests/integration.py` needs a separate OS
checkout and its host development dependencies; standalone users do not need it.

## Preflight, Audit And Input State

`doctor --target host|mips|all` checks existing tools, SDL headers, cross target,
sysroot/link-library paths, target runtime ABI and SDL license. `--template`
selects sdl2-c (default), native-c or native-cpp; `--gui` also checks Xvfb/xdotool.
It uses syntax-only stdin probes, never installs, downloads or writes a build.
It does not prove linking, display readiness or board compatibility.

`check-runtime` (alias `audit`) accepts a project/payload or .lilpck without
extracting/executing it. It checks every packaged ELF's machine/endian/O32,
MIPS32r2/legacy-NaN flags and PT_MIPS_ABIFLAGS (hard-float FP32), interpreter and
transitive DT_NEEDED. Search is exactly runtime/lib, then the supplied base root's
lib, usr/lib, usr/local/lib. No host fallback, inferred SONAME aliases, assumed
base libraries or automatic bundling. Missing base/dependencies fail with
actionable errors. RPATH/RUNPATH are rejected by this conservative profile.
JSON schema_version=1 has valid/errors/warnings and dependency providers; reports
exit 0/1 on stdout, invalid input JSON on stderr, usage errors exit 2.
This is not dlopen/module/symbol/script, license or device qualification.

The additive [input state API](include/lilsmoke/input.h) and
[SDL example](examples/input_state.c) provide held/pressed/released snapshots
and normalized per-pad axes with availability bits. Drain events through the
existing ls_input, then call ls_input_poll once per frame. M8 native keyboard
events stay digital-only: the legacy word does not carry analog floats.
Snapshots do not retain taps entirely between frames. Existing ls_input actions
and launchers remain supported. Frozen 0.1 and 0.2 artifacts are not overwritten.

## Lifecycle And Frame Pacing (0.3)

[lifecycle.h](include/lilsmoke/lifecycle.h) is an original, allocation-free C/C++
API. `ls_app_poll` samples aggregate input and advances a configurable held
Start+Select exit request (default 1500 ms). SDL_QUIT/window close request exit;
focus loss and hot-unplug reset combo progress. Requests are sticky until taken,
never injected host keys. Start/Select are distinct from generic Menu/Back:
Return is Start, Escape or Right Shift is Select; mapped SDL Start/Back or raw
buttons 7/6 supply pad state. The native independent-app adapter uses Return/Escape.

Studio saves and exits on the held request and displays frame/work/average
milliseconds, missed slots and combo progress in its existing status area.
`ls_frame_pace` uses SDL monotonic ticks and sleeping deadlines, skips overdue
slots without catch-up bursts, and bounds samples/history/counters for CPU-only
MIPS. No GPU/GL/X11 is added to the device profile. Integer 33 ms periods are
approximately 30.3 Hz, not exact 30 Hz or physical FPS guarantees.

[lifecycle.c](examples/lifecycle.c) is a complete example, with optional frame
limit for headless QA. The [API guide](docs/lilsmoke-applications-sdk.md#lifecycle-and-frame-pacing-03)
specifies wrap, focus, request ownership and timing limits. Release extraction
tests run C/C++ core tests under sanitizers and MIPS user emulation, SDL virtual
device tests under host sanitizers, and compile/link SDL tests/examples for MIPS.

Original SDK code/examples/docs: Copyright (c) 2026 José Pilas, MPL-2.0.
See [LICENSE](LICENSE) and [NOTICE](NOTICE). Existing OS components and third-party
libraries keep their own licenses. Keep SDL2 and toolchain runtime notices/source
obligations when distributing binaries; MPL-2.0 does not replace them.
