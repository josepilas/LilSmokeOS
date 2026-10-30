# Full Original Engine Development

This directory owns only the LilSmoke platform adapter for a private full-engine
build. It does not replace SM64 behaviors, physics, menus, camera or level logic.
Build tooling and evidence live in `tools/portsmoke/sm64-original/`.

## Release Blockers

The pinned DrUm78/sm64-port graphics directory explicitly forbids binary
redistribution. Private builds using `gfx_soft` are not `.lilpck` release artifacts.
Do not distribute their executables, generated source/assets, audio, ROM copies,
or private test screenshots. An asset-free executable and a binary-distributable
graphics implementation are both required before any BYO release.

The original ROM is read-only. Test builds use an isolated private copy and a
new HOME/save directory. The owner's private local package is separate from
public BYO releases. It may be tested on the owner's spare card; do not publish
or redistribute it. Shared emulator engines are not replaced by this package.

## Platform

The renderer remains the pinned CPU-only `gfx_soft` implementation. The adapter
uses SDL2 software surfaces rather than requiring SDL1.2, SDL_ttf, SDL_image,
OpenGL or a GPU/context. Scripted tests run the actual engine and original audio
synthesis on one thread; they do not send keys to a host window. Audio bytes and
images are private evidence, not bundled assets. FunKey process snapshots and
poweroff integration are intentionally excluded from this platform build.

## World Quality (Private 0.5.0)

The default `detail` preset starts at 256x192 and can use 320x240 when complete
frame-work measurements show sustained headroom. It never drops below 256x192.
HUD, menus and dialog remain 320x240. Reusable RGB565 surfaces keep the
text readable without rasterizing the entire 3D scene at the larger size. The
native HC framebuffer's 2D scaler presents the final 4:3 image. Every game
frame is rendered; this is not frameskip. The original game logic and camera
advance at their original 30 Hz target.
`saves/world-quality.txt` accepts `detail` (default, guarded 256/320),
`performance` (192x144), `balanced` (fixed 256x192), `quality` (fixed 320x240),
or `adaptive` (256x192 with a 192x144 floor).
`saves/render-width.txt` still accepts `160`, `192`, `256`, or `320`, and takes
priority over presets, disabling adaptive mode. Existing explicit overrides
are respected, not rewritten. Higher sizes trade CPU time for detail. No
hardware 3D API is used. Fixed 320 is an opt-in option, not a performance promise.

Detail predicts the larger frame's cost as current whole-frame work plus
9/16 of renderer work (the 320/256 squared pixel ratio). It requires six
consecutive 30-frame windows whose every-frame predicted peak is at most
29 ms, after startup warmup. A single 320 frame over 30 ms returns to 256 on
the next frame with a six-window cooldown. Busy scenes retain the 256 floor;
this does not promise 320 in every scene or guarantee physical FPS without
an M8 test. One reusable maximum world buffer avoids allocation while switching.

The software renderer now retains exact perspective endpoints across its
16-pixel spans. MIPS renderer objects use executable-only `-mno-shared -mplt`
code generation; the CPU ISA, original game floating-point rules and dynamic
musl ABI are unchanged. Nearest scaling uses exact fixed-ratio copies, and
optional bilinear scaling caches only two horizontal rows with identical
rounding. These changes do not substitute lower-detail models or game logic.

Adaptive mode measures complete frame work before pacing sleep. It uses
30-frame windows, skips the first two windows, and has a three-window cooldown.
It reduces 256 to 192 only when average work exceeds 36 ms and rasterization
(excluding presentation) consumes at least half the work. It increases only
after six consecutive windows at 15 ms or less at 192, leaving pixel-cost
headroom. It changes only render dimensions, not logic, input, audio or frame
delivery. It never chooses 160 or 320. This policy needs actual M8 validation.

`saves/world-scale.txt` accepts `nearest` (default, unchanged sharp scaling)
or `smooth` (optional, center-aligned integer bilinear world scaling). Smooth
costs additional CPU and affects the world only, before HUD/dialog drawing.
320x240 is an exact copy in either mode. Texture filtering remains off by
default; the existing `SM64_WORLD_FILTER=1` diagnostic override is unchanged.

The original 32 kHz stereo synthesis is converted to 44.1 kHz using an exact
320/441 integer phase with interpolation across input-block boundaries. A
bounded RAM queue retains unsent samples; the existing isolated native PCM
worker continues to own the M8 audio device. Failure/overflow counts are logged.
Normal gameplay no longer hashes every audio byte for QA.

`SM64_PROFILE` logs report work, rasterization, audio synthesis, presentation,
world scaling time, and active world dimensions. `SM64_QUALITY` logs transitions.
Raster time excludes presentation; scaling is also reported separately.
Profiles are emitted every five seconds. QEMU timing is diagnostic only, not a physical M8 FPS
measurement. Source-only renderer optimization is confined to the graphics
adapter; original game floating-point rules remain unchanged.
