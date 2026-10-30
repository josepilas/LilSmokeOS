# TreeFrog Ports Compatibility

Development checkpoint: 2026-10-29. This is a system compatibility component,
separate from LilSmoke Applications, not a fork or rebranding of TreeFrogUI.
New-build physical M8 acceptance is still pending. The human reports that the
preceding Hex-a-hop build works perfectly; ClassiCube is slow and awkward to control.

## Installation Contract

Copy the original companion `.sh` and game folder side by side into
**LILROMS/treefrog-ports**, retaining `<id>/config.json`, `run.sh`, engine,
subfolders and artwork. The native root is `/storage/treefrog-ports`. Open
**TreeFrog Ports** on the LilSmoke home screen. No `.lilpck`, manifest conversion,
engine recompilation, script editing or per-port registration is required.
Commercial game data is still supplied in the original port's documented folder,
exactly as with TreeFrog. This layer does not manufacture missing game assets.

The bundled legacy frontend companion resides at
`/storage/.lilsmoke/treefrog-runtime/cubegm/picoarch`. Native executables do not
need PicoArch. Libretro ports use this glibc frontend, never musl RetroArch.
The current development companion is an unmodified interoperability fixture;
its matching-source and redistribution audit must finish before a public OS
release containing it. No public release was created by this work.

## Research

Primary sources inspected:

- [PortsCollection installation and schema](https://github.com/tzubertowski/TreeFrogUI_PortsCollection).
  QA is pinned to commit `3920718f07218059af0c03f6669989cca4918eb6`.
- [Original native launcher](https://github.com/tzubertowski/TreeFrogUI_PortsCollection/blob/3920718f07218059af0c03f6669989cca4918eb6/ports/hexahop/run.sh)
  and [Libretro launcher](https://github.com/tzubertowski/TreeFrogUI_PortsCollection/blob/3920718f07218059af0c03f6669989cca4918eb6/ports/classicube/run.sh).
- [Shared data checker](https://github.com/tzubertowski/TreeFrogUI/blob/main/assets/ports/port_check.sh),
  [PicoArch platform/input implementation](https://github.com/tzubertowski/TreeFrogUI_picoarch/blob/r36sx/plat_sf3000.c),
  [native display interface](https://github.com/Kryptikone316/hex-a-hop/blob/sf3500/src/sf3500/hwdisp.cpp),
  and [native controller interface](https://github.com/Kryptikone316/hex-a-hop/blob/sf3500/src/sf3500/joykey.cpp).
- [TreeFrog licenses](https://github.com/tzubertowski/TreeFrogUI/blob/main/LICENSE.md).
  No CC BY-NC-SA frontend code, theme, logo or UI font is incorporated into LilSmokeUI.

The inspected original Hex-a-hop executable is ELF32, little-endian MIPS32r2/o32,
hard float, with `/lib/ld.so.1` and **glibc** dependencies. LilSmoke uses musl.
Its SHA-256 is `e7c1491af9da611d4ce6b83dd2243a1d53cf4117942123e16ab0a14763d8ee29`.
Renaming an engine or loading a foreign core into musl RetroArch cannot solve
that ABI difference. Some stock `lib/` files are actually ARM ELF; the matching
MIPS versions occur in `usr/lib/`. QA dependency selection checks ELF machine,
not just the filename. The real glibc loader also rejects the wrong architecture.

## Implementation

- Fresh bounded catalogue, separate from the ROM cache and emulator registry.
  JSON schema 1, IDs, relative paths, alternative data sets and file availability
  are checked using structured JSON and descriptor-relative filesystem access.
- Low-memory UI teardown/handoff to `lilsmoke-treefrog-launch <validated-id>`.
  The original `run.sh` remains the authority for arguments, working directory,
  game-data checks and save locations. Metadata is never evaluated as shell.
- A private RAM filesystem view exposes TreeFrog's absolute SD paths. Stock
  glibc/SDL live in that view's `/lib` and `/usr/lib`; the native musl BusyBox
  remains separately addressable. No musl preload enters a glibc game.
- Device driver aliases and `TF_DRIVER`/`PICOARCH_DRIVER` select **this device's**
  imported plaintext stock driver. They do not install another console's DTB,
  driver, kernel, framebuffer configuration or boot hook.
- A musl-side bridge reuses LilSmoke's production `Input`/`Keymap` implementation
  for evdev, configured mappings, HIDAPI shared state and hotplug. It publishes
  the original `/tmp/joy_key` SysV protocol. Its own legacy-input reader is
  explicitly detached to prevent button feedback. No keyboard/mouse injection,
  uinput remapper or Windows input automation is involved.
- The legacy protocol is a single-player word. Connected pads can control that
  player; this does not add multiplayer or new analog axes to old port engines.
  Left-stick directions and triggers map to the original digital protocol;
  Guide/Home maps to the original FN bit for frontends that handle it.
- A private RAM owner directory uses the same PID/start-time bakery protocol as
  the working M8 controller service, without `flock` or advisory `fcntl` locks.
  A failure logs the actual errno rather than calling every error a busy port.
  The engine
  has an owned process group. Start+Select exits; the mount owner completes
  cleanup before returning. No lazy unmount or forced storage-owner kill is used.
  Residual mounts cause the next launch to refuse, not silently stack new binds.
- Legacy child supervision adopts and reaps detached helpers. Cleanup leaves
  the view before unmounting and permits a bounded grace period for released
  references. It ignores late exit-chord signals during storage cleanup.
- The M8 loading worker acknowledges release before the glibc engine starts.
  The fixed RAM token is cleared without injecting a musl gate into that engine.

This view is **not a security sandbox**. Ports are executable code with the
device's existing privileges and access to its files/devices. Use trusted ports.

## Qualification

`tools/treefrog-compat/` contains reproducible catalogue, UI, input and MIPS VM
tests. Fixture downloads validate Git blob IDs and record SHA-256 inventories.
Only small selected runtime files are range-fetched, not a whole SDK/image.

Passed before the latest restart: ASan/UBSan catalogue and UI tests, complete
host UI build, MIPS UI build, and actual original-game execution. In the
128-MiB/one-CPU MIPS guest, original Hex-a-hop and ClassiCube both produced moving
frames and nonzero PCM. A selected-slot controller fixture triggered original
port input. Original release files remained byte-identical.

The guest uses the real stock glibc/SDL and unchanged engine/PicoArch binaries.
Its HC display/audio boundary is a QA frame/PCM capture driver; SDL fbcon uses
a virtual framebuffer. It is not an exact HC16xx SoC emulator and establishes
neither physical HDMI/DAC correctness nor M8 frame rate. No 3D GPU is provided.

Failed attempts are retained, not relabeled as passing. They exposed nested-bind
fixture issues, mixed-ABI stock dependencies, cleanup timing and a loading-token
handoff gap. A later 9p/DrvFS run stalled on host-backed I/O; FAT32 qualification
does not depend on that writable host filesystem.

The prior passing run is `_build/runtime-fat32-final3-20261028/report.json`:
128 MiB, one 74Kf CPU, no 3D GPU, real FAT32 on a temporary virtio disk.
Hex-a-hop produced 950 frames and 1,235,520 PCM frames; ClassiCube produced
1,260 frames and 1,069,920 PCM frames. Both PCM streams were nonzero, both exit
statuses were zero, loading tokens were released, all legacy binds disappeared,
and `fsck.fat -n` returned zero after unmount. All original files remained
byte-identical. These counters are execution evidence, not physical M8 FPS.

Physical feedback on October 28 exposed an earlier failure: both real logs
stopped at "another port is running", before either engine launched. That old
message conflated open/locking failures with contention, so it does not prove
the exact failing syscall or errno. The advisory lease has now been replaced.

The replacement passed `_build/runtime-ownerlock-20261028/report.json`, again
with 128 MiB, one 74Kf CPU and FAT32. Hex-a-hop produced 449 captured frames and
1,264,320 PCM frames; ClassiCube produced 120 frames and 748,320 PCM frames.
Both streams were nonzero, both exited zero, all binds/loading ownership were
released, originals remained byte-identical and the unmounted FAT check passed.
Ownership tests passed live exclusion, fork cleanup, PID reuse, crashed/empty
claims, symlink bounds, choosing peers, overflow and cleanup. These tests do not
replace a new physical M8 run after installation.

The preceding MIPS UI was byte-identical to the binary whose actual-UI VM
tests passed navigation, rescanning, two helper handoffs and responsive return.
Those UI handoff tests use a recording engine; the gameplay test above separately
executes the original engines. Neither test is presented as physical hardware.

## Installed Development Update

Verified on the known 32 GB SEGAM spare, D: LILOS / E: LILROMS. Only the UI and
five new compatibility files were added/rebound in the OS manifest. The legacy
companion and two original sample ports were copied to LILROMS. Existing
applications, ROMs, saves, settings, partition layout and stock boot files were
not replaced. No card repair/format or public release was performed.

Prior manifest SHA-256:
`fda577ead9cc8ee00a8ed91a707142043905b56f9ec4013b9be46bc8e6fa17a7`.
UI SHA-256: `79c61b892b45c81308ed14c5b1a016a21d416cfea949dc06526eb773ace93341`.
Transaction/readback record: `tools/treefrog-compat/_build/card-update-20261028/verification.json`.
The prior manifest and UI have small PC backups beside that record.

The subsequent ownership/return update is now installed and read-back verified:
`tools/treefrog-compat/_build/card-return-update-20261028/verification.json`.
Active manifest, including the subsequently verified console-session dependency,
is `02d79aa70c3c6cd777209fce59645b1dbef39aaced1bbebd3e72fe7904b96191`;
new UI is
`c6a9c6facb623bb06f0fb1ebb6bbe2a0c9d417f6cf3a7d2d72b651fe6df4a9bc`.
Only the bridge among the TreeFrog runtime files was replaced; both original
port folders and the stock companion remain unchanged. See
`development-checkpoint-20261028-return-treefrog.md` for the full narrow update.

Neither Hex-a-hop nor ClassiCube is a LilSmoke Application: their unmodified
files live in `E:/roms/ports/`, not `independent-apps`, and no `.lilpck` or app
installer is involved. The separately developed Undertale-LilSmoke package is
unrelated to this compatibility mechanism.

## October 28 File-Backed Root Repair

The next real-device logs passed ownership/input initialization but ended with
`TreeFrog original launcher: No such file or directory`. `run.sh` was present.
The M8 metadata-only root represents BusyBox and its musl interpreter/libc as
absolute links into `/os/files`. Binding `/bin` and `/lib` into a legacy chroot
without that namespace makes the shell's ELF/interpreter path unresolved.
The earlier VM had conventional files at those paths and missed this condition.

The launcher now additionally binds `/os` when `/os/files` exists, then remounts
that bind read-only. This does not replace the stock glibc namespace or inject
musl libraries into a glibc game. Cleanup owns this bind alongside the others.

New test cases reproduce the native BusyBox, musl loader and libc backing links:

- `_build/runtime-filebacked-negative-20261028/report.json` deliberately omits
  the backing bind. Both original ports reproduce exit 127 and the exact M8
  ENOENT message. Its passing flag means expected failure, not a working game.
- `_build/runtime-filebacked-readonly-20261028/report.json` uses the unmodified
  production launcher, 128 MiB, one 74Kf CPU and FAT32. Both original ports
  launch and exit zero. Hex-a-hop produced 657 captured frames/1,778,880 PCM
  frames; ClassiCube produced 720 frames/995,520 PCM frames. Both PCM streams
  were nonzero. The backing bind was observed read-only during each game;
  the source namespace remained writable afterward. All binds/loading tokens
  were released, original port bytes matched, storage unmounted and the FAT
  check returned zero. These are VM counters, not physical frame rates/audio.

The launcher-only fix is installed and read-back verified on the 32 GB spare.
No LILROMS, app, save, ROM, UI, kernel or partition writes were made.
`tools/treefrog-compat/_build/card-filebacked-update-20261028/verification.json`
records the final manifest
`68d2729df2deacfbd20e6fec9dddc9ff8fc76f7d4fc8843123e1d14973d77362`
and launcher
`42d02f9b66f2e5c8186d01aba8640011f4dfb2948b469fc7e64c377aa3cbd548`.

## Remaining Acceptance

The October 29 root migration and converted-app mode are described below.
The earlier card/VM records above retain their historical directory layout.

Safely eject both partitions and test native and Libretro ports on the M8,
including controller modes, sound, TV scaling and Start+Select return. Only
Hex-a-hop and ClassiCube have gameplay evidence so far; the other collection
ports must not be described as individually tested. Network services, physical
display/audio and other device profiles remain unqualified. Ports needing
original game data retain their upstream requirements, limitations and licenses.

## October 29 Direct Root And ClassiCube

Direct discovery now uses only `/storage/treefrog-ports`, displayed as
`LILROMS/treefrog-ports`. A private bind exposes that root at the old
`/mnt/sdcard/roms/ports` namespace for upstream scripts. The companion `.sh`
files may remain alongside their game folders; discovery reads the folders'
original `config.json` and executes their original `run.sh`. It never sources
the companion scripts or requires catalogue preparation. Old `roms/ports`
content is not automatically moved, copied or deleted by the OS.

Read-only M8 log evidence: `D:/lilsmoke/persist/logs/treefrog-classicube.log`,
SHA-256 `5e7d8b5175de9f54d52d6677030b851aa67d4fd760b4c2d9c5f552ad287b79ff`.
Fifteen diagnostics measure 43,338-63,153 microseconds between `retro_run`
entries (mean 51,827). These include frontend/presentation/audio/I/O work,
not just core CPU time, and are not a controlled frame-rate benchmark.
There are 39 PicoArch starts: the first game and 38 attempts to reopen the
missing `frogui_libretro.so` after exit.

Primary source checkpoints:

- [FroggyCraft README](https://github.com/tzubertowski/FroggyCraft/blob/bceecf9819ce98014f47bf7907817c228b505d4a/README.md):
  software rasterizer, reduced internal render size, gamepad-only controls.
- [Window and input implementation](https://github.com/tzubertowski/FroggyCraft/blob/bceecf9819ce98014f47bf7907817c228b505d4a/source/classicube_sf2000/upstream/src/sf2000/Window_SF2000.c):
  `sf2000-render-mode` modes 0=320x240, 1=160x120 (source default),
  2=80x60, 3=213x160; the HUD is upscaled separately.
- [Core wrapper](https://github.com/tzubertowski/FroggyCraft/blob/bceecf9819ce98014f47bf7907817c228b505d4a/source/classicube_sf2000/libretro/libretro_core.c):
  declares 60 Hz; no Libretro core-options registration for renderer tuning.
- [PicoArch core input](https://github.com/tzubertowski/TreeFrogUI_picoarch/blob/cbeb463b92c42773ef826bec3ae1bea72994963d/core.c)
  supports a digital joypad, not analog axes;
  [exit/handoff](https://github.com/tzubertowski/TreeFrogUI_picoarch/blob/cbeb463b92c42773ef826bec3ae1bea72994963d/main.c)
  execs the frontend path on return.

The scoped input adapter adds right-stick digital look through FroggyCraft's
existing X/B/A/Y look buttons. Original buttons, triggers, D-pad and left stick
are unchanged. This is not proportional analog aim; those face-button actions
also retain their upstream menu meanings. Hex-a-hop never enables this profile.
The build wrapper reuses the existing production Input implementation rather
than adding another controller enumerator or injecting keyboard events.

For direct ClassiCube only, the PicoArch alias is a RAM copy of our launcher:
it forwards the expected game core to the unchanged platform PicoArch and exits
cleanly when that binary tries to reopen TreeFrogUI. No upstream executable,
script, save, driver or configuration is overwritten. No guessed FPS cap,
frameskip, core timing override or forced lower rendering resolution is applied.
The log and upstream rasterizer suggest a real CPU/render cost, but attribution
and a quality/performance preset still need controlled M8 A/B measurements.

## Converted Application Mode

`lilsmoke-treefrog-launch --application APP_DIR` accepts a direct child of
`/storage/independent-apps/` with its own `meta.json`, exactly one root shell
launcher (any name), `adapter.json`, and the original tree under `runtime/port`.
The app's metadata ID is `org.lilsmoke.treefrog.<port_id>`; its display name and
directory may be renamed. Paths, JSON sizes, original data alternatives, ELF
architecture, libc family, links, special files and recursive tree budgets are
checked before mounts. ELF N32, FP64 and NaN2008 flags (`0x620`) are rejected.
Native writes resolve into its private application port tree, not a catalogue
copy. This remains execution of trusted code, not a security sandbox.

The frozen adapter uses `schema:1`, `kind:"treefrog-converted"`, `port_id`,
`profile:"native"|"libretro"`, `binary`, `content` (empty if absent),
`port_root:"runtime/port"`, `requires_runtime:"treefrog-compat-0.2"`, and
`binary_recompiled:false`. Binary/content/profile/ID must match original config.
Converted Libretro additionally requires `independent_libretro:true`,
`frontend:"runtime/frontend/lilretro"`, and `runtime_profile:"glibc"|"musl"`.

Converted Libretro never executes original `run.sh` and never requires PicoArch.
The helper constructs argv from parsed metadata:

```
/app/runtime/frontend/lilretro /mnt/sdcard/roms/ports/ID/CORE [CONTENT] --profile /app/runtime/launch.json
```

The whole selected app is bound at `/app`, with only its port tree at the
legacy alias. Policy save/system paths must be relative and stay inside the
application. The read-only gamepad transport file is exposed separately.
Glibc libraries search `/app/runtime/lib`, the port's `lib/` and top-level
libraries, then stock `/lib:/usr/lib`; musl uses only the native musl namespace.
Frontend interpreter and core DT_NEEDED libc must agree before execution.
Device glibc/driver prerequisites produce explicit errors and are never claimed
to be publicly bundled private firmware. Native converted apps keep the
original trusted BusyBox run script, syntax-checked before execution, including
setup loops and helper commands. This is not a sandbox. Converted Libretro
continues to reject setup its typed launch cannot preserve.

Build/CLI integration and qualification artifacts are tracked in
`tools/treefrog-compat/INTEGRATION-0.2.md`. No SD writes or migration are performed
by these runtime changes. The parent owns verified copy/migration and deployment.

## October 29 Delivery

The final helpers, new-root UI and launcher are installed/read-back verified on
the 32 GB SEGAM spare. Manifest:
`95e8c6984cd92e4d4962bb480fbb29e06e4e3a438e0cc9ca0fb7b4f67aecd5a9`.
All 76 historical port files and their subfolders were copied to
`E:/treefrog-ports`, preserving the originals and all game/user data. No stock
kernel, partition layout, ROMs or installed applications were replaced.

`runtime-root-app-20261029-final3/report.json` verifies original native and
Libretro ports plus actual native-app frame/PCM output. The virtual Cirrus
display mode is restored between launches, as the production UI normally does;
that VM boundary is not a new device-side display patch. Converted independent
Libretro is separately qualified with the real FroggyMania core/data through
the production installer/launcher in a 128 MiB MIPS VM. Both tests cleanly
release mounts and pass FAT32 checks. Physical M8 acceptance is still required.

See [direct ports and converter guide](treefrog-port-converter.md).

## Native04 Controller Follow-Up

The later native04 update replaces only the check helper, input supervisor and
launcher on the same verified 32 GB card. Active manifest is
`85120e975f25112136c938ae0afe9584628b1fd992e7084cb4207b7ff59eafc3`.
UI and stock boot files remain unchanged. Existing ports/user data are not
recopied. Tested native converted packages are placed in `E:/packages`, not
silently installed. Backup/readback results are in
`tools/treefrog-compat/_build/card-treefrog-native-20261029-04/verification.json`.

The input helper retains SDL stick events, avoids a neutral pad hiding another
pad's active stick, and tolerates short fresh snapshot contention. Start+Select
holds for 500 ms on one controller to exit direct and converted applications.
Chocolate Doom has an explicit horizontal-turn/trigger-strafe/stick-click
run/use profile; no vertical look or true dual-analog ABI is claimed. Both
supplied native archives pass direct and converted execution in the 128 MiB
MIPS VM; real independent ClassiCube and existing direct ClassiCube/Hex-a-hop
regressions pass too. These are functional QA captures, not physical speed or
controller acceptance. See the development checkpoint for exact artifacts.
