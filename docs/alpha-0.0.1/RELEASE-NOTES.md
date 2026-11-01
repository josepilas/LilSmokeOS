<!-- SPDX-License-Identifier: MPL-2.0 -->
# Alpha-0.0.1: Changes Since Pre-Alpha-0.0.1

**Release:** October 30, 2026. **Baseline:** Pre-Alpha-0.0.1, October 12, 2026.
Created and led by **José Pilas**.

The initial Alpha notes concentrated on licensing/publication and omitted
the intervening system development. This corrected account covers the actual
updated runtime: interface, themes, boot/loading, game discovery, emulation,
input, safe shutdown, media, TreeFrog compatibility and developer tools.

Applications, the Explorer, emulator selection, compressed ROMs and several
native engines already existed in Pre-Alpha. They are included below where
their behavior was repaired or extended, not represented as entirely new.

## Themes And Interface

- Expanded the EmulationStation-style engine used with EmuELEC/ArkOS theme
  sets: system-specific XML overrides, includes/variables, subset and aspect
  choices, normalized geometry, layered extras and stable drawing order.
- Added/refined horizontal and vertical system carousels and wheel layouts,
  with interpolated motion, wraparound and rapid-selection retargeting.
- Added `auto`, `slide`, `fade` and `instant` transition choices and bounded
  storyboard tracks for position, scale, opacity and rotation, with easing,
  delay, repetition and auto-reverse.
- Expanded game-list/grid presentation, cover/media placeholders, text
  metadata, date/rating widgets, help prompts and clock/battery overlays.
- Fixed shared background scenery sliding with the moving system page.
  Identical shared backgrounds stay stationary; different system artwork or
  explicitly animated backgrounds can still change by design.
- Fixed Power Off being lost when a themed screen was active, rather than
  treating a return to the home screen as successful shutdown.
- Improved theme geometry, clipping, logo placement, text alpha handling and
  pointer hit regions so visible controls and their interactive regions agree.

This is a software-rendered compatibility implementation, not the whole
EmulationStation engine. Theme preview videos use static artwork fallback,
not live video playback. Shader effects, arbitrary scripts and complete
upstream theme compatibility are not promised. See the
[current compatibility matrix](../theme-compat.md).

## UI Performance And Presentation

- Replaced delay-after-work pacing with a deadline-based 60 Hz target,
  avoiding catch-up bursts when a frame misses its deadline.
- Reduced repeated parsing, geometry calculations, glyph rasterization and
  image loading through bounded caches and reused alpha-modulated textures.
- Improved cache retention so a scan just beyond capacity does not continually
  evict all recently reused artwork.
- Added native dirty-region presentation and conservative full-frame/SDL
  fallback when the surface or device contract is unsupported.
- Added view-activity tracking and idle-frame reuse. Active animations,
  clocks, overlays, input and invalidated surfaces still request redraws.
- Coalesced pointer motion and improved event handling for mouse/touchpad use.
- Added/refined balanced/native UI rendering and TV safe-area configuration.
  The release defaults remain TV-oriented, not a forced handheld 4:3 menu.

These changes target reduced work and smoother interaction on constrained
MIPS hardware. A 60 Hz scheduling target is **not** a measured universal
60 FPS claim; large artwork and complex themes still cost CPU and memory.

## Boot Logo, Loading And Return To Menu

- Added the branded boot logo, displayed for at least six seconds and held
  through initialization until the UI is ready.
- Kept loading feedback visible across startup/game-discovery handoffs
  instead of intentionally leaving a long black interval.
- Added a blue progress bar with a percentage and stage updates. Progress
  reports startup/launch milestones, not a prediction of time remaining.
- Moved the logo higher above the loading bar and improved mode/scanout
  setup, retry handling and boot diagnostics.
- Added a loading wrapper around game handoff/archive resolution.
- Improved supervised return from games/apps: an unchanged ROM index and
  unchanged playlists can be reused rather than rescanning and rewriting
  the entire library on every return.

If the native logo backend cannot initialize, boot continues with diagnostics;
the logo is not allowed to make a recoverable boot failure permanent.

## Game Library And Navigation

- Added **Y** search in a game's list using the controller-friendly keyboard.
- Added fast downward traversal while **R2** is held.
- Added/refined **Settings -> Scan games now** and the scan-on-boot setting,
  cache invalidation and playlist refresh.
- Added **Recently Played**, **Favorites**, game options and **Start in
  Recents**. These collections use existing game entries and the selected
  emulator policy; they do not create duplicate ROM files.
- Improved warm return/library-cache handling while retaining an explicit
  manual rescan after external changes.

## RetroArch, Native Engines And Save States

- Fixed RetroArch menu zoom/viewport issues, the quarter-screen menu after
  opening a game, and restoring the game's native presentation on menu close.
- Fixed native presentation/cache lifecycle regressions that made menus
  clunky or affected previously working core performance.
- Restored Save State/Load State availability by shipping matching core
  information and not retaining stale capability metadata.
- Fixed PCSX4ALL save-state failures and their storage/write handling,
  including loading saved slots again after a reboot.
- Refined archive-to-engine routing, scaling, controller/menu/exit behavior
  and native audio integration for the adapted engines.
- Kept **Start+X** for the integrated emulator menu and **Start+Select**
  for exit/return where implemented by that engine's adapter.
- Kept PCSX4ALL as a bundled independent LilSmoke Application with its own
  icon/runtime and `open_core` PlayStation association. The clean public image
  seeds the public PCSX4ALL-LilSmoke 0.0.1 package; development-card package
  version strings need not match that public package's release number.

The registry covers classic consoles and data-engine systems, including Atari
variants, Neo Geo Pocket, WonderSwan, Pokemon Mini, Doom/PrBoom, Cave Story/
NXEngine, EasyRPG, ScummVM and OpenBOR integrations. Actual choices depend on
installed compatible providers. This is not a statement that every engine was
new in Alpha, ships in every profile, or supports every game/save-state format.
The GUI and launcher use the same provider availability checks.

PCSX ReARMed and generic N64 integrations remain experimental/CPU-limited;
PCSX4ALL is the preferred validated PS1 route on this M8 lineage. No Daedalus
runtime is part of this public OS release. See [emulation](../cores.md).

## Controllers, Keyboard And Cursor

- Fixed the RK707 receiver's X-Input and PS-Input paths in addition to the
  already working D-Input path, including mode-specific reports/mappings.
- Restored left/right analog input to RetroArch rather than only digital
  navigation working in the frontend.
- Improved reconnect/mode-change detection, initial input snapshots, stale
  button release, dropped-event recovery and device diagnostics.
- Added/refined Settings gamepad mapping with raw-input capture.
- Prevented unwanted stick-driven mouse/cursor behavior leaking into adapted
  emulator paths; refined pointer motion and cursor rendering in LilSmokeUI.

The owner confirmed RK707 D/PS/X modes and RetroArch analog operation. This
does not certify every dongle, hub, Bluetooth adapter or third-party gamepad.

## Power Off And Storage Safety

- Hardened the shutdown handoff with a RAM-resident, console-independent
  guard, explicit storage closure/checks and conservative failure handling.
- Added **Power off operation succeeded**, held for ten seconds before the
  screen becomes black when the guarded sequence succeeds.
- Kept warning/failure states distinct from success; storage needing repair
  must not produce an unconditional success indication.
- Fixed shutdown requests from themed screens.
- Improved stability around writable data, cached metadata and controlled
  application/emulator handoffs. Retained verified/atomic update mechanisms.

Safe Power Off reduces risk; it cannot guarantee against a failing card/reader,
power loss during writes or existing FAT corruption. A black screen by itself
is not proof of successful storage closure.

## Media And Terminal

- Added **Settings -> Shell Terminal**, a keyboard-operated Linux shell
  terminal rather than a simulated command prompt.
- Added FFmpeg and ffprobe tools.
- Improved the Explorer-launched music/video/photo player path: startup
  ordering, bounded decode queues, delivered-audio timing and redraw costs.
- Added/refined the native video-worker route, startup/duration tolerance,
  failure cleanup and software fallback.
- Improved video color conversion, overlay copying and pacing diagnostics;
  capped software fallback output at 640x360.

Music playback was confirmed working by the owner. High-resolution video,
unsupported codecs and native-worker/device allocation remain limitations;
these changes are not a promise of smooth HD playback or every-file support.

## TreeFrog Ports Compatibility

- Added a **TreeFrog Ports** home catalogue separate from Applications.
- Standardized copy-and-play content at **LILROMS/treefrog-ports**: the port's
  original companion `.sh` and game directory remain side by side.
- Added compatibility checks and a device-matched execution namespace for
  supported native/Libretro ports, rather than forcing a `.lilpck` conversion.
- Fixed file-backed `/os` mounts, supervised child cleanup, leftover mounts
  and relaunch/return behavior.
- Added/refined port input profiles, including ClassiCube right-stick digital
  camera controls and Chocolate Doom turning/strafing/run/use mappings.
- Added **Start+Select** exit in the direct compatibility path. The native
  TreeFrog bridge requires a roughly 500 ms hold on the same controller.

Original ports still require a matching CPU/libc, device libraries and game
data. Direct legacy Libretro ports also need their compatible PicoArch
companion. ClassiCube's GPU-free rendering and digital camera translation
have limits; compatibility is not identical engine behavior or universal speed.

## Applications, Converter And SDK/API

LilSmoke Applications remains the independent `.lilpck` ecosystem, with its
theme-independent grid, translucent-cube installer, bundled runtimes,
version-aware updates, rename/uninstall and emulator-app associations.
Root launch scripts can have **any name**, provided exactly one `.sh` exists
directly at the application's root. Native TreeFrog compatibility is separate.

New/expanded companion developer tooling is published independently:

- **[Port Converter 0.2.0](https://github.com/josepilas/LilSmoke-Port-Converter/releases/tag/0.2.0)**:
  local C/WebAssembly archive/ELF/ABI/dependency analysis, automatic adaptation
  planning and `.lilpck` output for supported native/Libretro cases. Converted
  Libretro packages use the independent `lilretro` frontend, not PicoArch.
  The [browser tool](https://josepilas.github.io/LilSmoke-Port-Converter/) has
  separate public `source` and `dist` branches.
- **[SDK/API 0.3.0-dev](https://github.com/josepilas/LilSmoke-SDK/releases/tag/0.3.0-dev)**:
  shell/C/C++/SDL2 starters, host preview, matching MIPS builds, dependency
  audits, validation, packaging, input/lifecycle/frame-pacing helpers,
  documentation and SDK Studio.

The converter is for ports the direct compatibility path cannot handle; it
does not compile arbitrary engines or translate foreign CPUs. Files remain
local to the browser. The SDK needs a matching external toolchain; browser/
host tooling memory requirements are not the console's 128 MiB budget.
These projects' source in the OS tree does not make the full toolchain an
on-device app.

## Separate Game-Port Releases

- [SM64-LilSmoke Pre-Alpha-0.0.2](https://github.com/josepilas/SM64-LilSmoke/releases/tag/Pre-Alpha-0.0.2):
  maintained separately, with user-ROM preparation from `original/` and
  extraction feedback; no original game data is supplied by the OS image.
- [Undertale-LilSmoke 0.0.1](https://github.com/josepilas/Undertale-LilSmoke/releases/tag/0.0.1):
  maintained separately, with user-supplied data preparation. The owner's
  successful gameplay/save tests do not grant redistribution rights to data.

The system-side application/return improvements support these ports without
bundling a user's ROM or installed games into the public image.

## PC Device Adapter And Guarded Services

The release retains the stock-folder Device Adapter in LILOS: a Windows
executable and Linux/macOS Python/Tk launchers. It inspects user-supplied stock
files, exports the needed private material and applies only a reviewed matching
profile with backup/recovery. Unknown revisions remain inspection/export-only;
stock-folder import does not supply an incompatible raw boot region or make
this image universal. macOS/new-board validation gaps remain explicit.

Wi-Fi/SSH, Bluetooth, USB network/LILDRIVE, ZRAM and clock settings retain their
driver/hardware guards. LILDRIVE is limited to LILROMS, with ownership rules to
avoid simultaneous console/PC writes. These retained facilities are not all
new Alpha additions, and their menu entries do not prove support on the M8.

## Image, Source And Card Provenance

- The Alpha image was assembled from the updated SEGAM runtime baseline.
  The card's read-only manifest check on October 30 matched that baseline:
  `85120e975f25112136c938ae0afe9584628b1fd992e7084cb4207b7ff59eafc3`.
- The public image uses clean settings/data, removes private SM64 prototypes
  and excludes Daedalus/custom Mupen experiments. It is not a clone of the
  user's installed games, saves, packages or personal configuration.
- LILOS is 1 GiB FAT32; LILROMS starts at 768 MiB FAT32 and grows at first boot.
  Its FAT capacity is prepared for cards up to 128 GiB. This retains the
  two-partition design; it does not introduce that design for the first time.
- The full authored external tree, C++ UI, services, board adapters,
  Buildroot integration and SDK/API source are public under MPL-2.0.
  Upstream licenses and vendor-source limitations remain documented.
- No new physical boot claim follows from public source availability.

## Validation And Remaining Limits

**Passed:** 50 Linux release tests; clean image FAT checks; a one-CPU,
128 MiB MIPS VM test covering first-boot expansion, UI navigation, SNES launch
from 7z, Start input and clean filesystem closure. A separate extended VM
run also reached the emulator menu and returned to LilSmokeUI.

**Not established:** a fresh physical cold boot of this exact clean Alpha
image, cycle-accurate HCSEMI emulation, physical FPS, audible HDMI/PCM quality
from VM captures, universal themes/controllers/codecs, or the production
power guard in the substitute VM framebuffer. Earlier owner-confirmed
device results are distinguished from release-image acceptance.

Overclocking, ZRAM, Wi-Fi, Bluetooth and USB gadget services remain subject
to the actual kernel/hardware. Their settings are not proof that a board
implements them. No GPU/GL/GLES/Vulkan capability is added by this release.

## Source Build Correction - November 1, 2026

- Restored the five missing QEMU/SF3000/GB350/VirtualBox defconfigs; the initial
  exporter recognized `.defconfig` but not Buildroot's `_defconfig` names.
- Fixed export retention without weakening content/security checks, and added
  export/wrapper regressions plus real pinned-Buildroot configuration checks.
- Added `--list-targets`, `--configure-only` and separate Buildroot/output
  overrides. Unknown targets fail before cloning; hardware-image gates remain.
- Added separately named source-r1/documentation-r1 archives for the corrected
  source/guides. See the [build guide](../build.md). No image/card write or retag.

Validation: 11 wrapper and 22 export tests pass; all five real Buildroot
2024.02.3 configurations and package graphs pass. Package compilation/booting
was not performed by these checks. Existing DirectFB/musl warnings remain
visible and require separate full-build validation.

## Documentation Snapshot Note

This expanded documentation was published after the initial Alpha upload.
The release tag, source ZIP snapshot, image bytes and original checksums are
unchanged. Corrected docs are on `main` and in the separate documentation ZIP.
The source inventory describes the original audited source archive, not every
subsequent documentation-only commit.
