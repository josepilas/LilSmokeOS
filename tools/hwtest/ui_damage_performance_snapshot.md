# Native Damage Presentation Handoff

Status: first working implementation verified; production Renderer sources
frozen for the parent's full UI build and card/device pass. Physical SEGAM-M8-V6
touchpad smoothness is NOT confirmed by these host or QEMU tests.

## Source Scope

This phase edits only `package/lilsmoke-ui/src/src/Renderer.cpp` and
`Renderer.h`, plus dedicated hwtests. It uses the parent's
`board/segam-m8-v6-stock/m8-damage.h` ABI via dlsym, without importing board
sources or changing any CLI/build scripts, Buildroot copies, card contents,
kernel, game/audio/BIOS paths, OpenDingux, or GUI layout/interface.

New test: `tools/hwtest/ui_damage_smoke.cpp`. The existing
`ui_cursor_input_smoke.cpp` also gains an absolute-touchpad click/drag test.
Artifacts remain isolated in WSL `/root/lilsmoke-cursor-qa-20261007`.

## Presentation Contract

- Native symbol: `lilsmoke_present_damage(const void *, unsigned width,
  unsigned height, unsigned pitch, const int32_t *rects, unsigned count)`.
  Only return 1 is success; count 0 is a successful no-copy frame.
- Only the verified SDL software window renderer uses this path. Explicit
  accelerated backends and render targets bypass it. Missing/disabled symbols
  use normal SDL_RenderPresent and allocate no frame shadow.
- After scene/cursor commands, SDL_RenderFlush completes software drawing;
  SDL_GetWindowSurface exposes the complete raster, not just the viewport.
- Surface checks: 32 bits / 4 bytes per pixel, Rmask=0x00ff0000,
  Gmask=0x0000ff00, Bmask=0x000000ff, and Amask either 0 or 0xff000000.
  Pitch must cover a row, be a multiple of four, and not exceed 16,384 bytes.
  Lock-required surfaces are locked while comparing, presenting, and committing.
- Surface dimensions are bounded at 1920x1080; the packed previous-frame
  shadow is capped at 8 MiB. Active 1280x720 storage is exactly 3,686,400 bytes
  (3.516 MiB). Maximum accepted storage is 8,294,400 bytes. Allocation failure
  falls back without an uncaught allocation exception.
- Whole rows first use memcmp. Changed rows are scanned in 64-pixel horizontal
  runs; adjacent dirty runs combine, and matching runs merge on consecutive
  rows. Rectangles are clipped to partial edge runs. The bounded flat-int32
  payload holds at most 512 rectangles, below the C helper's 2,048 cap.
- At half-frame dirty coverage or excessive fragmentation, one full-frame
  rectangle replaces the batch. Initial frames and changed surfaces synchronize
  in full. Identical visible frames call the native API with count 0, bypassing
  full SDL presentation. RGB-only equality ignores alpha/XRGB's unused byte
  because the C helper forces output opaque.
- Only successful native batches commit their copied regions to the shadow.
  Failure releases/invalidate the shadow and invokes real SDL_RenderPresent in
  the SAME frame. Native rejection remains on SDL fallback until a surface,
  window, or safe-area/presentation change; it does not allocate/retry every
  frame on unsupported modes.
- SDL window exposure, resize, restore/show/focus/display changes and render
  resets request full synchronization. Event-watch callbacks only set an atomic
  flag; the render thread owns memory cleanup. Surface pointer/pixels/dimension/
  pitch/alpha-mask changes also release the shadow. Destructor removes the
  event watch and frees the shadow before renderer/window closure.
- The native API wrapper, not Renderer, owns the parent's checkpoint/unblank/
  HDMI scaler side effects. There is no extra checkpoint call or duplicated
  SDL present after native success.

## Results

Host: SDL dummy, hidden WINDOWED software renderer, logical and actual output
1280x720. Native presentation is an exported injectable mock; it copies each
supplied rectangle into a simulated opaque front buffer. Linker-wrapped SDL
presentation records genuine fallback calls and synchronizes that mock front
buffer for fallback verification. No framebuffer or native host input IO.

`UI_DAMAGE_SMOKE_OK checks=3512 native_calls=579 sdl_calls=4`

Passed ordinary optimized host and ASan+UBSan with leak detection enabled.
Coverage includes real supplied cursor atlas motion and removal without trails,
full-front-buffer pixel equality, hover/clock/animation changes, count-zero
frames, padded pitch, clipped last tiles, vertical merging, alpha-only/XRGB
changes, 80 deterministic sparse random frames, unsupported masks/alpha/pitch/
dimension bounds, maximum shadow allocation, dense/fragmented frames, native
failure plus same-frame fallback, failed-shadow invalidation, exposure recovery,
safe-area changes, non-software bypass, surface replacement, and disabled API.

240-frame simulated pointer motion, with the same real scene/cursor raster:

| Measured Native Mock Requests | Full-Copy Control | Dirty Presentation |
| --- | ---: | ---: |
| Bytes requested/copied | 884,736,000 | 2,550,528 |
| Copy rows requested | 172,800 | 7,740 |
| Rectangles | 240 | 446 |
| Host whole fixture elapsed, optimized | 512.814 ms | 285.717 ms |

Full-copy control explicitly invalidates the test shadow each iteration so
the same API receives a full-frame request; the byte count is measured, not
just estimated. Dirty copy bytes are 0.288% of full-copy bytes in this fixture.
The host front-buffer mock uses cached RAM, not physical uncached hcfb memory;
these elapsed times and ratios do NOT establish device FPS, input latency,
or physical touchpad smoothness. Full software scene rendering still happens.

Host steady dirty profiling sample, 120 iterations:

`UI_DAMAGE_PROFILE frames=120 native_frames=120 sdl_frames=0 full_frames=0
rects=219 native_bytes_requested=1263360 copy_rows_requested=3870
shadow_bytes=3686400 flush_ms=0.685 compare_ms=0.508 native_copy_ms=0.008`

MIPS/musl: Renderer and main compiled with -Wall -Wextra -Werror, MIPS32r2/o32,
-mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -mlong-calls. The damage test and
its mock/export/wrapped-fallback path linked against target SDL libraries.
There is no target dummy video driver, so target rendering runtime remains
for the parent's full-image/physical pass.

`UI_CURSOR_INPUT_SMOKE_OK checks=448 input_event_bytes=24` on host and
`checks=448 input_event_bytes=16` on QEMU 74Kf. This adds actual production
parser coverage of absolute touchpad anchoring, motion source flags, click,
two ordered held-button drag packets, release, and subsequent motion. Existing
mouse/touchpad source barriers and fractional scaling remain unchanged.
`UI_TEXT_CACHE_SMOKE_OK checks=573` passed with the new absent-API fallback.

## Physical Verification

Set `LILSMOKE_UI_PROFILE=1` for the native UI. Existing UI_PROFILE records
SDL event/input dispatch/render submission/cursor/present/whole-frame work.
New UI_DAMAGE_PROFILE records every 120 presents: native and SDL frame counts,
full requests, rectangles, native bytes and copy rows requested, shadow bytes,
software flush, comparison and native-call average milliseconds.

The parent's M8Damage logs supply ACTUAL framebuffer copied bytes and copy
microseconds. UI requested counters include rejected requests; M8Damage is the
authority on actual copies. Main's present_ms includes all flush/diff/native/
shadow-commit/other overhead; component counters are diagnostic subdivisions,
not the whole present cost. No frame-cap sleep is included in frame_work_ms.

For a normal device run, native_frames should be nonzero and sdl_frames zero;
stationary frames should request no copies. Use `LILSMOKE_UI_NATIVE_DAMAGE=0`
at process startup for the original full-SDL-present control, with no shadow.
Physically verify touchpad movement, source switching, click/drag/release,
cursor visibility changes, hover, clock/animation, view transitions, and resume.
Obtain render/present timings on-card before declaring stutter fixed.

Remaining cost: every view still renders its full software scene and every
native-enabled frame compares a complete raster. Cold assets, complex alpha
blending, font/layout misses, or hotplug discovery can still stall. Native
rejection intentionally stays on fallback until presentation changes.
