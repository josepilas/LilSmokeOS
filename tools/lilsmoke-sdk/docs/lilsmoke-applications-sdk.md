<!-- SPDX-License-Identifier: MPL-2.0 -->
<!-- Copyright (c) 2026 José Pilas. -->
# LilSmoke Applications SDK/API 0.3.0-dev

This is an implemented, source-only developer SDK for SDL2 applications and the
existing Applications package/launch protocol. It is not a new OS runtime, emulator,
service bus, or promise of future ABI stability. Original SDK code, templates,
new header and this guide are MPL-2.0, Copyright (c) 2026 José Pilas.

## Available Now

### Lifecycle And Frame Pacing (0.3)

The original MPL-2.0 [lifecycle.h](../include/lilsmoke/lifecycle.h) is a header-only
C11/C++17 API with no SDL, allocation, OS service, floating point or host key
injection. [lifecycle.c](../examples/lifecycle.c) is a complete software SDL app.
The SDL glue and Studio starter use the same API:

```c
LS_InputState input = {0};
LS_FrameClock clock;
/* After ls_app_open: initializes app.lifecycle to a 1500 ms hold threshold. */
ls_frame_init(&clock, 33, SDL_GetTicks());
while (!app.lifecycle.exit_requested) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) (void)ls_input(&app, &event);
    ls_app_poll(&app, &input); /* sample aggregate held state, then lifecycle */
    /* update/draw, optional capture, present */
    ls_frame_pace(&clock);
}
/* Save as appropriate, then ls_app_close. No injected Escape/host key. */
```

`ls_lifecycle_init(state, hold_ms)` initializes focus and request state; zero
disables combo exit. Start and Select must BOTH be held in the aggregate mask
for the entire sampled interval, not just pressed in one frame. They may come
from different pads or keyboard aliases. Generic Menu/Back alone never suffice.
Return is Start; Escape/Right Shift are Select. Mapped SDL Start/Back and raw
buttons 7/6 supply distinct bits alongside the existing Menu/Back compatibility
bits. Escape is the existing native independent-app Select mapping; Backspace
is not Select (it can be a shoulder button). Native Home may share Escape: the
legacy key adapter cannot distinguish those sources. No analog data is invented.

`ls_lifecycle_update(state, held, now_ms)` advances `combo_held_ms`, bounded to
the configured delay, and requests `LS_EXIT_HELD_COMBO` once per hold. Releasing
either aggregate bit re-arms it. `ls_lifecycle_request_exit` ORs reasons;
`ls_lifecycle_take_exit` returns/clears them without re-arming a held combo.
Requests stay sticky across focus/unplug, so an already-requested exit is not
silently lost. `ls_input` requests `LS_EXIT_SDL_QUIT` for SDL_QUIT/window close
and retains the existing LS_QUIT action. The caller owns confirmation, save and
teardown. Studio saves/exits on the held combo or SDL quit, while its ordinary
menu Quit still asks for confirmation. Signals remain app-owned.

`ls_input` clears keyboard state and cancels progress immediately on focus loss;
keydown while unfocused is ignored. Focus gain starts a fresh hold, including
pads still physically down. Removal closes the pad and cancels progress even
if loss+gain/remove+add happen in one event drain. Other connected sources remain
held in the next snapshot. Call `ls_lifecycle_focus`/`ls_lifecycle_cancel_combo`
yourself for equivalent non-SDL transitions. Drain events every frame, including
while paused. The SDL event path also cancels a release/repress drained within
one frame, without cancelling an overlapping held alias. Pure snapshot-only
callers can miss releases/taps wholly between samples; the API is cooperative,
not a watchdog or system-wide exit shortcut.

`ls_frame_init(clock, period_ms, now_ms)` accepts integer 1..1000 ms periods,
returns 0 or -1 (invalid leaves the clock untouched). Initialize before use.
33 ms is approximately 30.3 Hz, not exact 30 Hz. `ls_frame_wait_ms` returns a
bounded wait to an absolute deadline. `ls_frame_pace` samples work-end time,
uses SDL_Delay until due (no busy spin), and calls `ls_frame_finish` with actual
wake time. Oversleep retains deadline phase; overruns skip expired slots in
constant time instead of producing catch-up bursts. No GPU, GL, X11 or vsync
is required on the device. The existing MIPS software/DirectFB profile is unchanged.

Stats have saturated uint32 frame/late/missed/clamped/reset counters. Delta,
work and lateness samples cap at 250 ms; `max_delta_ms` is the bounded lifetime
maximum. A fixed 64-entry uint16 ring and <=16000 ms sum give the integer recent
`ls_frame_average_ms`. No unbounded elapsed totals or per-frame allocations.
Work includes events/update/draw/present, excludes the pacing sleep; total delta
includes it. Missed counts skipped deadline slots, not rendered frames; late
includes even one millisecond of oversleep. Studio shows these measurements and
hold progress in its existing status area and opt-in JSON. They are application
timing diagnostics, not physical display FPS or target performance qualification.

All clocks use wrapping uint32 monotonic milliseconds, including SDL_GetTicks.
Unsigned differences work across one wrap provided successive intervals are
less than 2^31 ms (about 24.9 days). Backward/ambiguous frame timestamps reset the
deadline and increment `clock_resets`, never creating a huge sleep; a backward
combo clock starts a fresh hold. A suspended process can only react when it
runs again. The pure core accepts injected timestamps for deterministic tests;
timestamps are not wall-clock time. No precision smaller than one ms is claimed.

```sh
cc -std=c11 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) -Iinclude examples/lifecycle.c src/lilsmoke_sdl.c $(pkg-config --libs sdl2) -o lifecycle
./lifecycle
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./lifecycle 4
```

### Read-Only Preflight And Runtime Audit (0.2)

```sh
python3 -B lilsmoke_sdk.py doctor --target host
python3 -B lilsmoke_sdk.py doctor --target all --sdk "$LILSMOKE_SDK_OUTPUT" --gui
python3 -B lilsmoke_sdk.py check-runtime studio --base-root "$LILSMOKE_SDK_OUTPUT/target"
python3 -B lilsmoke_sdk.py audit SDK-Studio-0.3.0-dev.lilpck --base-root "$LILSMOKE_SDK_OUTPUT/target"
```

`doctor` checks Python, Linux/WSL, existing compiler/pkg-config/SDL2 headers,
cross compiler triple, sysroot/link-library paths, target loader/SDL2 ABI and
the existing SDL2 license. `--template native-c|native-cpp|sdl2-c` selects
requirements; `--target all` includes both cross compilers. `--gui` checks
existing Xvfb/xdotool. Only syntax-only stdin compiler probes are used: no
object/executable outputs, installs, downloads, system writes or SDK mutation.
Success is a preflight, not a link/display/runtime test. An unset display is a
warning, since headless builds remain useful. Windows receives an actionable
Linux/WSL build-platform error but can still scaffold/package/audit.

`check-runtime`/`audit` read a project, payload directory or ZIP .lilpck, optionally
with `--runtime TREE` merged under runtime/. They do not extract or execute
anything and do not invoke ldd/readelf. Package path/CRC/size/link checks still
apply. Every ELF is inspected, including unused bundled libraries. The stricter
audit verifies ELF32, little-endian, MIPS, O32/MIPS32r2, FP32/legacy-NaN flags,
PT_MIPS_ABIFLAGS version/ISA/register/FP ABI and musl interpreter. Missing ABI
flags fail as unverified instead of being called hard-float. This reads the
hard-float ABI value separately from the FP register width; see the
[GNU FP ABI reference](https://sourceware.org/binutils/docs/as/MIPS-FP-ABI-Selection.html).

DT_NEEDED is traversed transitively, with cycles bounded, without guessing:
exact regular-file names in payload/runtime/lib first, then lib, usr/lib and
usr/local/lib under `--base-root`. Base-image absolute symlinks are resolved
inside that image root, never through host /lib. The ELF interpreter must exist
at its absolute location inside the supplied base root. No base libraries are
implicitly assumed. A missing provider fails, identifies the requester and
dependency, and suggests supplying the correct base tree or explicitly bundling
the licensed target DSO. SONAME alone does not create a filename alias. Nested
library directories and embedded RPATH/RUNPATH are outside this conservative
profile; the latter fail. The launch environment must actually include
runtime/lib in its library search path, as the OS Applications launcher does.

Both commands return JSON schema_version=1, sdk_version, valid, errors and
warnings; each finding has code/message/action and runtime findings also have
path. Audit includes ELF facts and requester/provider/origin dependency edges.
A completed report is stdout with exit 0 (valid) or 1 (errors); malformed input
is JSON on stderr with exit 1; argparse usage errors exit 2. Shell-only audit
reports NO_ELF as a warning, not a shell dependency qualification.

Limits: existing payload budgets, 1024 ELF nodes/20000 edges/256 MiB inspected bytes, 256
program headers, 8192 dynamic entries and 1 MiB dynamic string tables. Audit
does not resolve symbols/versions, dlopen modules (including SDL2 DirectFB),
script commands, preloads, CPU instruction compatibility, licenses or board/VM
behavior. Nothing is automatically bundled or repaired. `validate` retains its
0.1 structural role; use both commands before release.

### Source-Level Input State (0.2)

[input.h](../include/lilsmoke/input.h) is an additive C/C++ snapshot helper;
[input_state.c](../examples/input_state.c) is a complete SDL2 application.
Existing LS_Action/ls_input callers and the native keyboard adapter remain valid.

```c
LS_InputState input = {0}; /* once, before the loop */
/* Each frame, after draining SDL events through ls_input(&app, &event): */
ls_input_poll(&app, &input);
if (input.pressed & LS_BUTTON_ACTIVATE) { /* one rising edge */ }
if (input.held & LS_BUTTON_LEFT) { /* continuous hold */ }
if (input.released & LS_BUTTON_LEFT) { /* one falling edge */ }
```

The held mask aggregates keyboard aliases and connected SDL pads; releasing one
alias/device does not release another held source. Edges compare successive
snapshots, not event counts: sub-frame taps can be missed. Focus loss clears
keys, buttons and axes; hot-unplug releases absent sources on the next snapshot.
Native keyboard state tracks delivered keydown/up events, including adapters
using SDL_PushEvent. Repeated keydown does not create a new snapshot press.

Each pads[i] reports connected, instance id, mapped, held, axes_available and
axes[6]. SDL mapped controller order is LX/LY/RX/RY/LT/RT; signed axes normalize
to [-1,1] including asymmetric int16 endpoints; mapped triggers to [0,1].
Raw joystick axes 0..5 retain backend ordering, with no semantic mapping promise.
Availability bits distinguish a centered supported axis from an unavailable one.
No deadzone/filter is applied to these floats; digital navigation uses a separate
threshold. M8's legacy digital keyboard word does not supply analog floats:
native_keys mode exposes digital events and no pad axes. No physical controller,
touchpad or analog capability is claimed by synthetic SDL tests.

Build the example with existing host tools (output is local):

```sh
cc -std=c11 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) -Iinclude examples/input_state.c src/lilsmoke_sdl.c $(pkg-config --libs sdl2) -o input-state
./input-state
```

Run commands in this guide from the extracted `lilsmoke-sdk` directory.
`lilsmoke_sdk.py` uses Python 3.10+ stdlib only. Host commands
`new`, `validate`, and `package` work on Windows and POSIX. `build` is an explicit
Linux/WSL convenience for native MIPS builds and an SDL2 host preview. `run`
executes that preview; it does not install, emulate or run a device package.

```sh
python3 lilsmoke_sdk.py new notes --id org.example.notes --name "Developer Notes" --launcher "Developer Notes.sh"
python3 lilsmoke_sdk.py validate notes
python3 lilsmoke_sdk.py package notes --output Developer-Notes-0.1.0.lilpck
python3 lilsmoke_sdk.py validate Developer-Notes-0.1.0.lilpck
```

Use your installed Python executable on Windows. The SDK does not download
Python or any SDK. Tool success prints JSON to stdout and exits 0. `run` instead
inherits the application's output and exit code. Validation/build
errors print JSON to stderr and exit 1; argparse usage errors exit 2.
Existing projects, packages and native binaries are never silently overwritten.

`new` creates:

```text
notes/
  sdk.json                   host template selection; not packaged
  .gitignore                 host-only
  src/main.c or main.cpp     C/C++ templates only; not packaged
  src/lilsmoke_sdl.c         SDL2 template only; not packaged
  include/lilsmoke/*.h       C/C++ templates only; not packaged
  _build/                    local logs/manifests; not packaged
  payload/
    meta.json
    Developer Notes.sh       --launcher selects the filename; default run.sh
    LICENSE
    NOTICE
    runtime/bin/application  created by a native build, not a placeholder
```

The shell template is a useful small note application: it invokes the existing
foreground keyboard with a 128-character limit and writes accepted text to
app-local `userdata/note.txt`. Cancel returns without writing. The native C11
and C++17 templates print the actual launch context/optional logical ROM path
to the app log and exit; they deliberately do not claim graphics or emulation.
The `sdl2-c` template is the interactive SDK Studio: a 640x480 software-rendered
color/tone/volume editor with pointer and gamepad navigation, pause/quit menus,
queued stereo PCM and atomic settings save/reload. It has no GPU requirement.
Edit the author/description and retain or adjust licensing appropriately for
your own additions. The original example code remains MPL-2.0.

Only `payload/` is bundled. This boundary prevents ordinary host project files
from being swept into a package, but it is not a secret scanner or sandbox.
Review payloads yourself. Do not put saves, personal configurations, keys,
credentials, proprietary game assets, SDKs, or host binaries there.

## Actual Package Contract

The existing OS helper is `/usr/lib/lilsmoke/lilsmoke-apps`. Its source of truth
is `package/lilsmoke-apps/src/lilsmoke-apps.cpp` and
`package/lilsmoke-scripts/src/AppsProtocol.h`. No runtime code is copied into
this SDK and no runtime change is needed to use its packages.

The OS detects supported archive formats by contents. SDK 0.1 intentionally
emits and validates only ordinary ZIP (stored or deflated), named `.lilpck`:

```text
meta.json                    directly at archive root
SDK Studio.sh                any safe name, exactly one root .sh (case-insensitive)
runtime/bin/...              optional target executables
runtime/lib/...              optional explicitly bundled runtime dependencies
runtime/assets/...           optional redistributable application resources
```

There must be no wrapper folder. Subdirectories may contain additional scripts.
The SDK requires scripts to be UTF-8, LF-only, NUL-free and begin `#!/bin/sh`.
It does not execute scripts during validation, check shell semantics, or certify
their behavior. The OS launches the root script with `/bin/sh`; it does not
select a launcher from a metadata field.
Use `new --launcher "SDK Studio.sh"`, rename the sole payload root script later,
or use an uppercase `.SH` suffix. Spaces are supported; quote paths in commands.
A second root shell script is rejected even if `entrypoint` names the first.
Nested helper scripts are allowed. A basename before `.sh` is required.

```json
{
  "id": "org.example.application",
  "name": "My Application",
  "version": "0.1.0",
  "architecture": "mipsel-linux-musl",
  "author": "Developer",
  "description": "A native application",
  "license": "MPL-2.0"
}
```

The OS requires name/version and can derive an omitted ID. The SDK deliberately
requires a stable explicit ID: 1..128 ASCII letters/digits/dot/underscore/hyphen,
starting alphanumeric, without `..`. Preserve it across updates/renames. Names
and every path component must fit the FAT-compatible 120-UTF-8-byte rule:
no controls, slashes, `:*?"<>|`, reserved DOS names or trailing spaces/dots.
Application names cannot start with a dot. SDK versions accept one to three
numeric components, optional dotted prerelease and build identifiers, at most
64 characters. The OS compares `1`, `1.0`, `1.0.0` equally and ignores build
suffixes; the SDK is stricter about suffix syntax and does not order releases.

Unknown JSON fields are preserved. Duplicate keys, non-finite numbers, NULs,
invalid UTF-8, metadata over 64 KiB, and nesting beyond 16 levels are rejected.
If supplied, architecture must be `mipsel-linux-musl`. This metadata is advisory
to the current OS, not a capability negotiation or compatibility guarantee.

An optional relative `icon` must reference a bundled regular file <=2 MiB.
The SDK does not decode it or certify image dimensions. Use a modest, actual
icon supported by the target UI; missing icons use the UI's existing fallback.
The SDK includes no borrowed icons or private customization artwork.

SDK safety limits are intentionally tighter than the OS installer's limits:
20,000 explicit entries, 256 MiB expanded/compressed archive cap, 64 MiB per
file, 1,024 UTF-8 bytes per path, 32 path components, 256 KiB per shell script,
8 MiB ZIP directory. Split, encrypted and ZIP64 archives are refused. Links,
hard-linked source files, reparse points, special files, traversal, ambiguous
case/prefix collisions, reserved installer state and common credential/VCS
paths are refused. Raw ZIP names are checked before Windows separator
normalization. All member contents are streamed for byte counts/CRC validation;
an excessive advertised member is rejected before expansion.

The existing OS installer supports larger archives and more archive formats;
SDK rejection does not imply OS rejection. This conservative SDK profile is not
a hostile-process sandbox. Source/runtime directories must be trusted and stable
during packaging; adversarial concurrent filesystem mutation is not covered.

Packages use sorted entries, fixed ZIP timestamps/modes, canonical JSON and
deflate. Same inputs on the same Python/zlib implementation are byte-repeatable;
different zlib versions need not produce identical compression bytes. Packaging
refuses existing output, streams directly to a new file, verifies it on success,
then reports SHA-256. A process/power kill can leave an incomplete output: do not
distribute until exit 0, and run `validate` after transfer. There is no package
signature, automatic publishing, installation, or SD access in this SDK.

## Prerequisites And SDL2 Quickstart

The source ZIP contains Python tools, public headers/helpers, templates, tests,
documentation, MPL-2.0 and notices. It does NOT bundle a compiler, sysroot,
SDL/DirectFB binary, OS source, OS image, emulator, game or proprietary asset.
No private OS source is needed for standalone generation/build/preview/package.

Host preview needs Linux or WSL, Python 3.10+, `cc`, `pkg-config`, SDL2 development
headers and the matching host SDL2 runtime. On Debian/Ubuntu these are normally
provided by `python3`, `build-essential`, `pkg-config`, `libsdl2-dev`. A usable
X11/Wayland desktop (or WSLg) is required for a visible window. These tools must
already be installed; this SDK never installs packages. Verify without changes:

```sh
python3 --version
cc --version
pkg-config --modversion sdl2
pkg-config --cflags --libs sdl2
```

```sh
python3 lilsmoke_sdk.py new studio --template sdl2-c --id org.example.studio --name "SDK Studio" --launcher "SDK Studio.sh"
python3 lilsmoke_sdk.py build studio --target host
python3 lilsmoke_sdk.py run studio
```

Arrows navigate/adjust, Ctrl/Space/A activate, Esc/Alt/Backspace go back,
Enter/F1/Pause open the menu. Mouse/touch activate controls. SDL game-controller
A/B/Start map to activate/back/menu; unmapped joysticks use buttons 0/1/7 (6 also
back), axes 0/1 and hats. Axis actions are threshold crossings, not held repeats.
The example saves on explicit Save and clean exit, including confirmed Quit.
Focus loss pauses. Audio failure is visible and does not prevent editing/saving.
Host saves live in `studio/_build/host/userdata/studio.ini`, not in the payload.
`build --host` is a shorthand for `build --target host`.

`validate studio` intentionally fails before a MIPS build, even after a host
build: a host executable is never inserted into a device payload. Host preview
is the usable standalone path when the matching cross SDK is not available.

MIPS build additionally requires a SEPARATELY supplied, matching Linux-hosted
MIPS32r2 little-endian O32 FP32 hard-float legacy-NaN musl Buildroot SDK/output.
An ARM/aarch64 SDK, glibc SDK, or arbitrary MIPS toolchain is not interchangeable.
`--sdk PATH` (or `LILSMOKE_SDK_OUTPUT`) expects the output ROOT, containing:

```text
host/bin/mipsel-buildroot-linux-musl-gcc
host/bin/mipsel-buildroot-linux-musl-g++       for native-cpp
host/bin/mipsel-buildroot-linux-musl-readelf
staging/                                    compiler sysroot, includes and link libraries
staging/usr/include/SDL2/SDL.h
staging/usr/lib/libSDL2.so                   matching target SDL2 link library
target/lib/ld-musl-mipsel.so.1                matching device musl loader
target/usr/lib/libSDL2-2.0.so.0               runtime; may resolve to a file inside PATH
build/sdl2-<version>/LICENSE.txt              one existing unambiguous SDL2 license
```

Keep the SDK's relocatable compiler/sysroot layout intact; copying just `gcc`
is insufficient. This tool does not provide or download that cross SDK, and no
public download location is claimed here. Obtain a matching redistributable
SDK/output from the platform maintainer with its license/source notices. An
SDK missing `build/sdl2-*/LICENSE.txt` cannot use the automatic SDL2 profile as-is.

The tested local path in WSL Ubuntu-24.04 as root is
`/root/lilsmoke-os/build/buildroot/output`. This is a local example, not a path
expected to exist on another developer's machine:

```sh
export LILSMOKE_SDK_OUTPUT=/root/lilsmoke-os/build/buildroot/output
python3 lilsmoke_sdk.py build studio --target mips --sdk "$LILSMOKE_SDK_OUTPUT"
python3 lilsmoke_sdk.py validate studio
python3 lilsmoke_sdk.py package studio --output SDK-Studio-0.1.0.lilpck
python3 lilsmoke_sdk.py validate SDK-Studio-0.1.0.lilpck
```

The SDL2 profile bundles ONLY the target SDL2 shared library (dereferenced to a
regular file under its loader name), its license and your executable. It checks
ELF identity and `DT_NEEDED`: the executable may require SDL2 and musl; SDL2 may
require only musl. Unexpected direct dependencies are refused, not guessed.
This is NOT a complete dependency closure: the tested SDL2 dynamically loads
`libdirectfb-1.7.so.7`. Its matching DirectFB/libdirect/libfusion libraries,
modules/configuration and board display/input/audio adapters must already exist
on the target OS; they are not bundled in this ZIP or automatically collected.
That tested target SDL2 has DirectFB enabled and no X11/dummy video backend;
do not try to run the MIPS build as a host desktop preview. The host build uses
the independent host SDL2 installation. Both render in software, not OpenGL.

Builds refuse existing output. To rebuild, deliberately remove only the old
`_build/host/application` for host, or the generated
`payload/runtime/bin/application`, `payload/runtime/lib/libSDL2-2.0.so.0`, and
`payload/runtime/licenses/SDL2.txt` for MIPS SDL2, then build again. Do not remove
user saves. Inspect `_build/host/build.log` or `_build/mips/build.log` on failure.
Each build writes its source hashes and runtime hashes in `build-report.json`.
Native static C/C++ builds instead use `_build/build.log` and `build-report.json`.

## Runtime Bundles And Native Build

An explicit `--runtime TREE` on directory validation/packaging merges TREE under
archive `runtime/` without copying into the project. TREE is reviewed just like
payload: no symlinks or collisions. For example, TREE can contain `bin/engine`,
`lib/libexample.so.1`, `assets/...` and `licenses/...`. Supply real regular files
under the exact loader names. Do not bundle an entire sysroot. There is no
automatic dependency collection, ELF dependency closure, runtime download,
license clearance, stripping of arbitrary third-party binaries, or guessing of
which host library belongs on the device.

```sh
python3 lilsmoke_sdk.py new native --template native-cpp --id org.example.native --name "Native Example"
export LILSMOKE_SDK_OUTPUT=/root/lilsmoke-os/build/buildroot/output
python3 lilsmoke_sdk.py build native --target mips
python3 lilsmoke_sdk.py validate native
python3 lilsmoke_sdk.py package native --output Native-Example-0.1.0.lilpck
```

The owner SDK path above is an example, not an embedded default. `build --sdk
PATH` also works. This SDK uses the existing `host/bin/mipsel-buildroot-linux-musl-
gcc` or `g++`, not a download. Flags are C11/C++17, `-Os -mips32r2 -mtune=74kc
-mabi=32 -mfp32 -mhard-float -static`, section GC and stripping. These match the
existing MIPS musl CPU target and add no 3D dependency. Example compilation is
serial, nice 10, pinned to one permitted CPU, limited to 768 MiB address space
and 180 seconds. Only the project payload and `_build/` are written; SDK/target
global installation never occurs. Native scaffolds cannot be packaged before
their real binary exists. Deliberately remove an old binary before rebuilding.

All detected ELF payloads must be ELF32 little-endian MIPS32r2/O32/FP32 with no
ABI2/FP64/NaN2008 flags. A dynamic interpreter, if present within the bounded
64-KiB program-header inspection, must be `/lib/ld-musl-mipsel.so.1`. This is
identity screening, not proof of compatible instructions, hard-float attributes,
static libc provenance, symbol versions or complete dependencies. Build with the
actual target SDK and test on a qualified runtime. Static example output still
contains third-party toolchain libraries: audit their notices/source obligations
before distributing binaries. The SDK's MPL license does not replace them.

## Launch Environment And C API

The installed layout is `/storage/independent-apps/<display name>/`. The existing
helper runs the script in that directory and supplies `LILSMOKE_APP_DIR`,
`LILSMOKE_APP_NAME`, `LILSMOKE_APP_ID`, and `HOME` (the app directory).
`runtime/lib` and `lib` precede inherited `LD_LIBRARY_PATH`. Preserve the helper's
preload/device adapters; do not clear them in an app wrapper. The existing
launcher requests DirectFB and software SDL rendering, clears DISPLAY/Wayland,
and selects board-specific input/audio adapters. This SDK neither implements
nor guarantees those adapters for every board or graphics API.

The frontend is destroyed before app exec on the low-memory console; no second
resident UI is needed. Exit cleanly and release display/input/audio resources.
On the stock supervisor, returning from the app restarts the UI. Do not fork a
resident daemon in the examples or assume a live UI object is callable.

The original SDK header is `include/lilsmoke/app.h`:

```c
#include <lilsmoke/app.h>
const char *identity = lilsmoke_app_id();
const char *name = lilsmoke_app_name();
const char *directory = lilsmoke_app_directory();
char logical_path[8192];
int status = lilsmoke_open_rom_path(logical_path, sizeof logical_path);
/* 0: lexical path; 1: normal menu launch; -1: bad environment/buffer + errno */
```

The three getters return borrowed `getenv` pointers, possibly NULL. The path
helper joins the existing Open Core variables with the ROM root and rejects
lexical escapes/control characters. It does not open/canonicalize a file, detect
symlink escapes, extract archives, validate the core against a runtime registry,
or establish a sandbox. Callers remain responsible for filesystem use. The
header is not installed into the OS or the global SDK by this work; include it
from your project's include directory. There is no shared `liblilsmoke` ABI.

## Open Core And Keyboard

`open_core` can be a system string or nonempty array; the SDK checks its versioned
copy of the actual `EmulatorSystems.h` registry, canonicalizes aliases/case and
removes duplicates. Integration compares the full registry with the actual
header. Examples: `gbadvance` becomes `gba`, `genesis` becomes `md`. Use
`new --open-core gba --open-core md` only when implementing those engines. The
example programs are not emulators; adding metadata does not create one.

The actual helper command is `run-core APP_ID CORE INPUT`. It validates the
installed ID/declaration and ROM containment, then invokes the root script with
exactly two quoted arguments: `core=gba` and `rom=subfolder/Game Name.gba`.
Identical values are supplied as `core`, `rom`, `LILSMOKE_OPEN_CORE` and
`LILSMOKE_OPEN_ROM`. The filename is relative to `/storage/roms/<core>/`, or to
`LILSMOKE_ROM_ROOT/<core>/` when configured. Archive names are unchanged. Normal
Applications launches pass zero arguments and clear those four variables. Never
`eval` them or split ROM filenames on spaces. An app can invoke the existing
`/usr/lib/lilsmoke/lilsmoke-rom CORE PATH` resolver explicitly if its real engine
needs extracted content; this SDK does not bundle or emulate that resolver.

The existing text service is a foreground executable, not a new SDK service:

```sh
if text=$(/usr/bin/lilsmoke-keyboard --title Name --max-length 128); then
    printf '%s\n' "$text"
fi
```

Options actually implemented: `--title` (<=160 bytes), `--initial`,
`--max-length 1..4096`, `--numeric`, `--secret`. Text is printable ASCII;
numeric restricts to digits. Exit 0 prints accepted text plus newline; 1 means
cancel/signal/quit, 2 invalid input, 3 initialization failure, 4 output failure.
The caller must release/suspend display/input before this synchronous helper,
then reacquire afterwards. Do not use `--initial` for secrets: argv is visible
to other processes; `--secret` only masks display, not stored data.

An existing OS `<lilsmoke/keyboard.h>` provides `lilsmoke_request_text` (0 accept,
1 cancel, -1 error), using spawn/pipe/wait rather than shell interpolation.
Its source and license are not copied or relicensed into this SDK. Use the
installed OS/staging header only when available under its own license. The new
SDK header deliberately does not invent a second keyboard API or promise SDL
text-field interception.

## Updates, Trust And Limits

The actual installer owns files included in a package. Updates preserve
user-created files not overwritten by package paths, remove obsolete owned
files, preserve the user's renamed app name, and identify releases by ID.
Uninstall removes the app folder and its user data. Keep mutable data outside
the package-owned file set. Native applications are trusted, unsandboxed code
with access to storage; metadata is not a permission system. No OS service for
cloud saves, network access, notifications, accounts, secrets, permissions,
hot-reload or background jobs is provided or promised by SDK 0.1.

`include/lilsmoke/sdl.h` and `src/lilsmoke_sdl.c` are a small implemented SDL2
source-level helper, not a generic OS GUI service or stable binary ABI. There
is no GUI builder, debugger integration, store/signing client, hardware 3D
abstraction, general dependency bundler or backward-compatibility guarantee.
Developers can extend the copied source or use qualified SDL/POSIX directly.

## SDL2 Source API

Compile the copied `src/lilsmoke_sdl.c` with SDL2 and `-Iinclude`. The build command
currently compiles `src/main.c` and this helper; adding more sources or other
libraries requires extending your build. It is a starter, not a build-system
generator or arbitrary-source discovery service.

```c
#include <lilsmoke/sdl.h>
LS_App app;
if (ls_app_open(&app, "Example", 0)) return 3;
int running = 1;
while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        LS_Action action = ls_input(&app, &event);
        if (action == LS_QUIT || action == LS_BACK) running = 0;
    }
    ls_begin(&app, 0xf1f4f4);
    ls_text(&app, "EXAMPLE", 24, 24, 3, 0x20282b);
    /* Optional ls_screenshot(&app, "frame.bmp") belongs HERE, before Present. */
    ls_present(&app);
    SDL_Delay(33);
}
ls_app_close(&app);
```

- `ls_app_open(app, title, capture_audio)` returns 0/-1; video is required,
  audio is optional (`audio_failed` is set on failure). The app owns SDL globally:
  do not share this lifetime with another SDL owner. Close releases its devices,
  queued audio, controller handles, capture buffer, renderer/window and SDL.
- `ls_input` consumes one polled SDL event and returns `LS_NONE`, directional
  actions, `LS_ACTIVATE`, `LS_BACK`, `LS_MENU`, `LS_PAUSE` (focus loss), `LS_QUIT`
  or `LS_POINTER`. Pointer coordinates are in `app.pointer_x/y`. SDL scales real
  window events to the 640x480 logical renderer; injected events must already
  use logical coordinates. Native OS input mode preserves its key adapter and
  avoids opening joysticks twice. Synthetic tests are not physical-device proof.
- `ls_begin`, `ls_fill`, `ls_line`, `ls_text`, `ls_present` draw in 640x480 logical
  space with `0xRRGGBB` colors. Text is a limited original uppercase 5x7 bitmap
  alphabet, scales 1..4, at most 100 characters; it is not Unicode text layout.
- `ls_screenshot` returns 0/-1 and saves the current backbuffer as BMP. Draw
  first, capture, then present. SDL invalidates the backbuffer after Present;
  redraw before a later capture. It is not a capture of the desktop or final
  display adapter. Output is limited to 1920x1080; PNG conversion is external.
- `ls_audio_pump(app, enabled, volume)` returns 0/-1; pumps a 440 Hz triangle tone
  as 44.1 kHz stereo signed 16-bit PCM, clamps volume to 0..100 and bounds the
  SDL queue. Disabling clears/pauses it. It is not a mixer or a game audio engine.
  `capture_audio=1` allocates at most one second of PCM; `ls_audio_wav` exports
  that opt-in capture. A nonzero dummy-sink WAV is not proof of physical sound.
- `ls_data_path` constructs a bounded single leaf in APP_DIR/userdata (or cwd),
  creates the directory and rejects traversal/separators. Parent paths and
  launch environment must be trusted; this is not a sandbox.
- `LS_Settings` contains color 0..7, tone 0..1 and volume 0..100.
  `ls_settings_load` returns 1 loaded, 0 missing, -1 corrupt/error without
  overwriting the caller's defaults. `ls_settings_save` returns 0/-1; it writes
  `userdata/studio.ini` using a temporary file, file fsync, rename and directory
  fsync where supported. This is a specific example schema, not general INI.

Only opt-in `LILSMOKE_SDK_TRACE=1` emits Studio state JSON, including pointer
coordinates. The example also accepts `--test-frames N`, `--test-report PATH`,
`--test-screenshot PATH` and `--test-audio PATH`. These write to caller-selected
paths for QA and are not ordinary package assets or telemetry services.

## Verification

Standalone pure tests: `python3 -B -m unittest discover -s tests -p 'test_*.py'`
(SDK, runtime/doctor and source-release tests). SDL2 2.0.14+ is required for the
controller availability API and virtual-device QA. Linux host GUI QA additionally needs
existing Xvfb, xdotool and host SDL2 development files (Pillow is optional for
PNG conversion); it starts an authenticated private X server without altering
WSLg or requiring a desktop session:

```sh
python3 lilsmoke_sdk.py new qa-studio --template sdl2-c --id org.example.qa --name "SDK Studio"
python3 lilsmoke_sdk.py build qa-studio --target host
python3 -B tests/test_studio.py --project qa-studio --out _build/host-qa
```

Use a fresh project without existing preview userdata and a new output path.
Tests fail on a blank capture, failed click outcome, missing/nonzero-PCM failure,
save/reload mismatch, server exit or startup deadline; no device is accessed.
`tests/release_workflow.py --archive PATH --out NEW_PATH --sdk SDK_OUTPUT` checks
an extracted source ZIP: all pure tests, doctor, C/C++ header ASan/UBSan,
input/lifecycle-example host/MIPS compilation, C/C++ lifecycle sanitizer and
static MIPS core execution, host SDL virtual raw/mapped split-source combo,
release/focus/hotplug/quit and real-delay pacing sanitizer tests, custom shell launcher/package, SDL2 host
build/run/capture/reload, MIPS SDL2 build/validate/audit/deterministic package,
missing-base rejection and direct QEMU 74Kf C/C++ native/header examples.
MIPS SDL lifecycle tests/examples compile and link against the actual target
SDL; they do not execute with a host dummy driver. Target DirectFB execution,
physical controller/timing and hardware acceptance belong to the parent.
Omit `--sdk` for host-only; its report marks
MIPS checks unqualified. This never runs full-system QEMU or installs anything.
Add `--gui` to also run the real Xvfb keyboard/mouse/audio/save/reload test
against a fresh project generated by the EXTRACTED SDK. It requires the host
GUI QA dependencies above. Only test ZIPs you trust: this workflow executes the
extracted Python tools and compiles their examples.

Repository integration ONLY (requires the separate existing OS checkout and
host libarchive/json-c/libsodium development files; NOT needed to use the SDK):

```sh
python3 -B tests/integration.py --out _build/check --sdk /root/lilsmoke-os/build/buildroot/output
```

Omit `--sdk` to avoid cross-compilation. The runner compiles the current actual
Applications helper into the new SDK's ignored `_build/`, compares the actual
registry, tests install/normal launch/Open Core/rename/update/userdata/uninstall,
and checks hostile package rejection. Recording launchers and named ROM fixtures
are explicitly test-only, not gameplay. It checks the new header as C and C++
with host ASan/UBSan. Optional MIPS checks compile/run both real static examples
and the header test under `qemu-mipsel -cpu 74Kf` user emulation. This is not a
full VM, UI/keyboard integration, actual gameplay, device I/O, physical-board,
graphics, audio, save-reliability, or performance qualification.

### Offline Deployed MIPS Helper

An optional `--existing-mips-helper PATH` requires `--sdk` and reads a deliberately
host-staged helper copy. Never point it at a physical card. It records its input
size/SHA-256 and verifies they remain unchanged. All installs/ROM fixtures for
this check live under a disposable Linux `/tmp` directory. The command prefix is
`qemu-mipsel -cpu 74Kf -L SDK/target`; target shared libraries are read from the
existing SDK, not copied or installed.

The generated native C package is inspected and installed unaltered. Its
installed ELF hash and original launcher bytes must match the package. Then a
test-only recording shell temporarily replaces that disposable installed
launcher. The exact MIPS helper performs normal `run` and `run-core`, proving
app-directory/ID/name/HOME, cleared stale variables, exact argc/argv, canonical
core and nested ROM filename environment values. An undeclared core must fail.
The ELF remains unchanged and the original launcher is restored before cleanup.
Declaring `gba` in this ABI fixture is not a claim of emulation capability: the
native C example is diagnostic code only.

This explicit fixture is necessary because QEMU user-mode `exec('/bin/sh')` may
hand control to the WSL host shell. No system binfmt setting is inspected or
modified to force a nested MIPS launch. Real C/C++ examples are independently
executed directly by MIPS user emulation. The JSON deliberately records
`recording_shell_fixture=true`, `full_mips_exec_chain_qualified=false`,
`native_execution_from_helper_qualified=false`, and
`physical_ram_128_mib_qualified=false`. A passing helper ABI result does not
override those limits.

Helper QEMU children get one CPU, nice 10, a 180-second timeout and 4 GiB of
virtual address space to accommodate a roughly 2 GiB 32-bit PIE reservation.
That limit is neither a physical allocation nor a simulated device RAM size.
Compiler children retain their independent 768 MiB virtual limit. User emulation
does not qualify operation within 128 MiB physical RAM.

```sh
python3 -B tests/integration.py --out _build/deployed-check --sdk /root/lilsmoke-os/build/buildroot/output --existing-mips-helper /path/to/offline/lilsmoke-apps
```

Qualification output is local test evidence, not public SDK content. Do not
publish `_build/`, host paths, generated binaries, fixtures or personal data as
the source SDK. No SD, global SDK writes, OS/UI edits, TreeFrog runtime edits,
runtime asset redistribution, or publication is performed by this assignment.
