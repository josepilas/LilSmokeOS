# Theme Motion QA

This harness links the production SystemView, GridView, ThemeRender, Renderer,
Font and Theme implementations. It creates no app replacement and never launches
the desktop UI. SDL's dummy video driver and software renderer provide direct
framebuffer captures. Executable-only linker wrapping of SDL_GetTicks supplies
explicit timestamps while SDL's own internal timing is left alone.

Run in Ubuntu 24.04 WSL from this project:

```sh
QA_MIPS=1 sh tools/ui/test_theme_motion.sh
```

Outputs default to `build/theme-motion-20261021`; isolated source snapshots,
executables and the tiny optional initramfs default to
`/root/theme-motion-qa-20261021`. Override QA_MOTION_OUTPUT, QA_MOTION_WORK,
QA_THEMES_ROOT, QA_FONT or SDK_OUTPUT as needed. Dependencies are g++, Python 3
with Pillow, SDL2/SDL2_image/SDL2_ttf development packages and ffmpeg. Optional
MIPS checks use the existing read-only SDK compiler, BusyBox, musl and kernel,
plus qemu-mipsel and qemu-system-mipsel.

The default theme source is the existing four directories under
`/root/lilsmoke-vm-validation/storage/themes`. Provenance samples match those
files against the four supplied ZIP archives. Archives are never extracted or
duplicated, including nested archives. New extraction is deliberately not
implemented while physical SSD free space is below the 15 GiB floor.

## Checks

- ASan and UBSan, including leak detection and fail-fast diagnostics.
- Deterministic 0/100/250/500 ms frames and 25 Hz authored-time sequences.
- 320x240, 856x480 and 1280x720 production view rendering for all four themes.
- Shortest-path carousel wrapping, 2000 rapid retargets, tick overflow, reset,
  zero/one-entry motion and fade endpoints.
- Horizontal/vertical and both wheel layouts; two-entry duplicate-logo bounds
  during movement and when settled; pointer activation at painted logo centers.
- Instant/fade/auto styles, synthetic game-media fade and asset-free fallback.
- Nested MotionScope restoration, base image/text behavior outside explicit
  scopes and application-overlay rendering after a scope exits.
- Alpha-only text fades reuse the identical glyph texture and cache allocation;
  pixel checks also verify modulation and subsequent opaque rendering.
- Optional static MIPS numeric executable under qemu-user and an actual Linux
  kernel on Malta/74Kf with one CPU and 120 MiB RAM. The initramfs is a tiny
  independent QA archive, not a rootfs/disk clone.

`report.json` and per-job `pixel-analysis.json` contain machine-readable results;
per-job `sanitizers.log` records C++ assertions and sanitizer output. Source
SHA-256 manifests identify the exact snapshot used for compilation.

`host-software-motion-preview.webm` and the four keyframe contact sheets are
explicitly labeled host software QA. They demonstrate authored motion and real
theme layout, not M8 footage, game-video playback, measured frame rate or physical
console performance. Game artwork is synthetic; no ROM or game is launched.
Neither SDK/target, production files, physical storage devices nor SD cards are
modified. The existing Y-search/R2 navigation test is not edited or replaced.
