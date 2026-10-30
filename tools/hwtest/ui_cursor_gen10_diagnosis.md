# Gen10 Physical Cursor Diagnosis and Frozen UI Patch

Production source frozen: Renderer.cpp and Renderer.h only. No edits to main,
home/catalog/Settings/OpenDingux integration, Input/Cursor/Font, board helpers,
backend libraries/scripts, Buildroot staging, card, kernel, RA, game/audio/BIOS
paths. Tests and this snapshot are under tools/hwtest.

## Actual Physical Evidence

Read-only evidence: D:/lilsmoke/bringup.log, gen10 section lines 4494, 4521-4522,
4563, 4590-4591 and 4630. All three UI instances reject the first native damage
submission; both complete 120-frame profile windows use only SDL fallback.

| Physical Window | Motion Present | Zero Motion Dispatches |
| --- | ---: | ---: |
| Frames | 120 | 120 |
| Input events / motion dispatches | 382 / 28 | 134 / 0 |
| Input dispatch ms/frame | 0.761 | 0.948 |
| Render submission/work ms/frame | 102.524 | 85.207 |
| Cursor draw ms/frame | 0.204 | 0.004 |
| Present ms/frame | 172.426 | 162.452 |
| Entire frame work ms | 275.980 | 248.681 |
| Native / SDL frames | 0 / 120 | 0 / 120 |

These are aggregated real-device windows, NOT controlled same-view motion/idle
trials. The zero-motion window still has button/settings activity. They establish
that rendering plus fallback presentation dominates, while input and the cursor
draw are tiny. The 16 ms cap cannot repair a 249-276 ms work interval.

The native call was actually attempted: full_frames=1, rects=1,
native_bytes_requested=3686400 and copy_rows_requested=720. Therefore the source
passed the UI's surface format/dimension checks; this was a C API rejection, not
an SDL source-format rejection or an absent symbol. Shadow storage is then freed,
and the old rejection latch prevents all further native attempts. There are no
M8Damage copy records in these gen10 sections. The card launcher exports
LILSMOKE_HCFB=1.

The exact C-helper rejection branch is NOT identifiable from gen10's old log.
M8Display reports valid hcfb geometry/masks after SDL fallback, not before the
first native attempt. Deferred first-present mode activation is plausible but
unconfirmed. Parent owns new stage/errno/actual-geometry diagnostics; no geometry
repair or hardware-mode guess is made here.

## Finite Patch

- API failures no longer latch native presentation off permanently. They keep
  same-frame SDL fallback, discard the shadow, and retry with 100/200/400/800/
  1600/2000 ms capped exponential backoff. Success clears the delay. Each retry
  starts with a full frame, since no failed frame is committed to the shadow.
- Invalid SDL source formats, bounds, allocation or flush failures still fail
  closed until a surface/window/presentation change. No GPU or driver workaround.
- Rejection diagnostics now include fresh errno, result, source dimensions,
  pitch, pixel format, rectangle count, HCFB environment and retry delay.
  A one-time native-active record proves the API has succeeded.
- Fully opaque fillRect uses SDL_BLENDMODE_NONE/bulk fill instead of the scalar
  per-pixel blend loop, then restores the previous method's BLEND follow-on state.
  Translucent fills are unchanged. clear() was already a bulk fill; the fix is
  especially relevant to the additional opaque full-screen theme-background fill.
- Opaque outline optimization was deliberately removed after exact pixel testing
  caught different SDL outline/degenerate-edge coverage. drawRect stays legacy.
- UI_DRAW_PROFILE adds native attempts/rejections and requested opaque/blended
  fill pixel areas to the existing frame and damage telemetry. Requested areas
  are logical rectangle areas before clipping, not actual framebuffer writes.

No static-base cache, skipped animations, velocity prediction, resampling,
pointer speed changes, altered source barriers or action/drag coalescing rules.
All views still render; image blending/scaling, layout and other CPU costs can
remain after the opaque fill fix. Frame timing improvement must be measured on
the physical target, not inferred from host ratios.

## Verification and Paths

- tools/hwtest/ui_damage_smoke.cpp: 3561 host checks passed, including ASan+UBSan
  with leak detection, injected EIO capture, recovery without a window event,
  capped persistent-failure backoff, event-driven reset, failure fallback and
  complete visible-RGB/opaque-front-buffer equality. Native copy benchmark remains
  884736000 full-control bytes versus 2550528 dirty bytes over 240 frames, with
  7740 dirty copy rows and 446 rectangles. No physical framebuffer IO in tests.
- tools/hwtest/ui_opaque_draw_smoke.cpp: exact legacy-reference pixels across
  ARGB8888, RGB888 and ABGR8888, base alpha 0/17/255, clipping/viewport,
  mixed transparent/opaque fills and unchanged outlines, random small rectangles,
  edges/degenerate geometry, preserved follow-on blend mode and full 1280x720 fill.
  Host/ASan passed; target SDL's software surface path passed on QEMU 74Kf without
  a dummy video driver. --smoke-only skips the benchmark for target runs.
- tools/hwtest/ui_text_cache_smoke.cpp: 573 checks passed with the new Renderer.
- Target Renderer object and damage/opaque tests compiled/linked with musl,
  -Wall -Wextra -Werror, MIPS32r2/o32, 74kc tuning, DSPR2 and hard float.

First optimized host opaque-fill run, 160 interleaved full-screen samples:
legacy median 6.6703 ms, bulk median 0.5759 ms; p95 12.6159 / 1.2202 ms.
This is a synthetic software-surface microbenchmark under concurrent host load,
not device performance. Sanitizer timing is not a performance estimate either.

WSL artifacts: /root/lilsmoke-cursor-qa-20261007/opaque-draw,
opaque-draw-asan, opaque-draw-mips, damage-retry, damage-retry-asan,
damage-retry-mips, Renderer-retry-mips.o, text-cache-retry.

## Next Physical Pass

Rebuild/copy/card integration belongs to parent. With LILSMOKE_UI_PROFILE=1,
correlate the new Renderer rejection source/errno with the C helper's exact stage
and geometry. A native-active log plus native_frames > 0 and M8Damage actual
copies proves recovery. Persistent rejection still requires the board-side
diagnosis/fix; retries are not a substitute for valid geometry or working mmap.

Compare same-screen stationary and touchpad-motion windows using UI_PROFILE,
UI_DAMAGE_PROFILE, UI_DRAW_PROFILE and actual M8Damage copy timing. Confirm click,
drag/release, source changes, hover, cursor disappearance and animations. Physical
cursor smoothness remains unconfirmed until this run; no RA or OD regression
claim is inferred from these isolated UI tests.
