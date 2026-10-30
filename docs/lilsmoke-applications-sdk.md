<!-- SPDX-License-Identifier: MPL-2.0 -->
<!-- Copyright (c) 2026 José Pilas. -->
# LilSmoke Applications SDK 0.3.0-dev

Public [SDK/API repository](https://github.com/josepilas/LilSmoke-SDK) and
[0.3.0-dev release](https://github.com/josepilas/LilSmoke-SDK/releases/tag/0.3.0-dev)
provide source, documentation and SDK Studio. This host/developer tool is
published separately from the OS image and its installed user applications.

The complete [standalone SDK/API guide](../tools/lilsmoke-sdk/docs/lilsmoke-applications-sdk.md)
now lives inside the SDK, alongside its [README](../tools/lilsmoke-sdk/README.md),
so relative documentation links also work after extracting the source ZIP.
Original SDK sources, examples and docs are MPL-2.0, Copyright (c) 2026 José Pilas.
Existing OS components and third-party libraries keep their own licenses.

## Quickstart

From the repository root on Linux/WSL, using existing Python 3.10+, `cc`,
`pkg-config` and SDL2 development headers/runtime, plus X11/Wayland or WSLg:

```sh
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py new tools/lilsmoke-sdk/_build/my-studio --template sdl2-c --id org.example.studio --name "SDK Studio" --launcher "SDK Studio.sh"
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py build tools/lilsmoke-sdk/_build/my-studio --target host
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py run tools/lilsmoke-sdk/_build/my-studio
```

The SDL2 starter is interactive: software rendering, action/pointer navigation,
pause/quit menus, queued stereo audio, atomic settings save/reload. It uses public
`include/lilsmoke/sdl.h` and `src/lilsmoke_sdl.c`, not private UI classes. Capture
the drawn backbuffer before `ls_present`; SDL invalidates it after Present.

For MIPS, supply a separate matching Buildroot SDK. The source release does NOT
bundle compiler/sysroot/runtime binaries or private OS source. `--sdk` expects
`host/bin/mipsel-buildroot-linux-musl-{gcc,g++,readelf}`, an intact `staging/`
sysroot with SDL2 and target link libraries, `target/` musl/SDL2 runtime, and one
`build/sdl2-*/LICENSE.txt`. A generic ARM/glibc SDK is not interchangeable. The
guide details DirectFB/libdirect/libfusion/modules and board dependencies that
the target OS must already supply; they are not automatically collected.

```sh
# Tested LOCAL path in WSL Ubuntu-24.04 root; not a bundled/public SDK path.
export LILSMOKE_SDK_OUTPUT=/root/lilsmoke-os/build/buildroot/output
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py build tools/lilsmoke-sdk/_build/my-studio --target mips --sdk "$LILSMOKE_SDK_OUTPUT"
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py validate tools/lilsmoke-sdk/_build/my-studio
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py package tools/lilsmoke-sdk/_build/my-studio --output tools/lilsmoke-sdk/_build/My-Studio-0.1.0.lilpck
python3 tools/lilsmoke-sdk/lilsmoke_sdk.py validate tools/lilsmoke-sdk/_build/My-Studio-0.1.0.lilpck
```

Only `payload/` is packaged. A host preview never satisfies target validation.
The sole root launcher can have any safe `.sh` name, including spaces/uppercase
extension; `--launcher` names it at creation. Metadata does not choose it.
Commands refuse existing outputs. Native C/C++ diagnostic and shell keyboard
starters, runtime bundling, exact launch/Open Core/keyboard API, licenses and
limits are documented in the standalone guide. No SDK/OS install or card writes
are performed by these tools; the parent project owns delivery/integration.

## Lifecycle And Pacing

0.3 adds the original MPL-2.0 allocation-free `lifecycle.h` core, distinct
Start/Select held bits and `ls_app_poll`. Aggregate Start+Select held for 1500 ms
requests exit without injecting host keys. SDL_QUIT/window close request exit;
focus changes and pad removal cancel combo progress without losing a completed
request. Studio saves and exits cleanly on the request, and exposes frame/work/
average milliseconds, missed slots and hold progress in its existing status area.

Monotonic millisecond deadlines sleep without busy-waiting, preserve phase,
skip expired slots and bound samples/history/counters. The 33 ms period is
approximately 30.3 Hz, not exact 30 Hz. Device software SDL/DirectFB stays intact;
there is no new GPU/GL/X11 requirement. See the standalone guide for cooperative
polling, native key aliases, wrap limits and the working `examples/lifecycle.c`.

## Verification

0.2 adds read-only `doctor --target host|mips|all` and
`check-runtime PROJECT-OR-PACKAGE --base-root SDK-OUTPUT/target` (alias `audit`).
Reports include structured actionable findings; runtime audit checks every ELF's
machine/endian/O32/MIPS32r2/FP32 hard-float ABI and transitive DT_NEEDED against
exact bundled/base providers, with no host fallback or automatic dependency copy.
The additive input snapshot helper supplies held/pressed/released and normalized
axes only when the SDL joystick/controller backend actually provides them.
The M8 native keyboard adapter remains digital-only. See the standalone guide
for API, search-profile limits and example. The frozen 0.1 source ZIP/package
remain immutable, as do frozen 0.2 artifacts; new artifacts use 0.3.0-dev names.

```sh
python3 -B -m unittest discover -s tools/lilsmoke-sdk/tests -p 'test_*.py'
python3 -B tools/lilsmoke-sdk/source_release.py --output tools/lilsmoke-sdk/_build/release/LilSmoke-Applications-SDK-0.3.0-dev-source-final.zip
taskset -c 0 nice -n 10 python3 -B tools/lilsmoke-sdk/tests/release_workflow.py --archive tools/lilsmoke-sdk/_build/release/LilSmoke-Applications-SDK-0.3.0-dev-source-final.zip --out tools/lilsmoke-sdk/_build/extracted-check-030 --sdk "$LILSMOKE_SDK_OUTPUT" --gui
```

Host graphical QA and repository helper integration are separate checks; see
[verification details](../tools/lilsmoke-sdk/docs/lilsmoke-applications-sdk.md#verification).
Neither the SDK extraction workflow nor user-mode QEMU qualifies a physical
board, its 128 MiB budget, physical audio or real controllers. Full-VM evidence
belongs to the parent harness and must identify its exact package/engine hashes.
