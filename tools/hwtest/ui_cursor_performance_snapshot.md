# Native UI Cursor Performance Snapshot

Status: source saved, scoped host/sanitizer and MIPS checks completed; physical
SEGAM-M8-V6 cursor smoothness and DirectFB timing remain unmeasured.

## Scope

- Edited only `package/lilsmoke-ui/src/src` and dedicated UI hwtests.
- No board/native-video, RetroArch QuickMenu, CLI/build scripts, card, kernel,
  or OpenDingux changes. No native desktop input injection.
- UI sources: `Input.h`, `Input.cpp`, `Font.h`, `Font.cpp`, `Renderer.h`,
  `Renderer.cpp`, `main.cpp`, and new `PointerMotionBatch.h`.
- Tests: `ui_cursor_input_smoke.cpp`, `ui_text_cache_smoke.cpp`,
  `ui_cursor_perf.cpp`. Build artifacts live outside the Buildroot tree at
  `/root/lilsmoke-cursor-qa-20261007` in WSL Ubuntu-24.04.

## Changes

- Evdev reads 64 records at once and retains unread records across `poll()`
  returns. Native buttons, keyboard text, SYN_REPORT packet boundaries,
  SYN_DROPPED handling, and same-packet click coordinates are unchanged.
  Extra buffer storage is 1,024 bytes/device on the target ABI (1,536 on host).
- Main dispatch coalesces completed, unheld pointer-motion packets to the last
  actual packet position. All actions, wheel events, pointer-source changes,
  and primary/secondary/middle drags are barriers. Per-packet clamping and
  fractional mouse/touchpad remainders stay in Input. No smoothing filter,
  invented samples, extrapolation, or changed pointer-speed law.
- At most 256 logical input events are drained per main iteration, to bound
  dispatch work before rendering. Excess records remain buffered for later;
  no buttons or packets are discarded.
- Main's 16 ms cap now measures from the current iteration's start. It no
  longer stores a pre-sleep timestamp as the next frame's start, which had
  caused alternating short/long intervals. This is the existing approximate
  frame cap, not a claim of 60 Hz delivery on the real device.
- Renderer owns an LRU cache of text textures keyed by monotonic font-face ID,
  text, and full RGBA color: at most 128 entries and 1 MiB of pixel storage.
  Strings over 4,096 bytes or textures over 1 MiB are drawn without retention.
  Key/container overhead is additional, bounded by those entry/string limits.
- Styled font handles have a 16-entry LRU in addition to the default font.
  Reload/eviction uses new face IDs, preventing stale texture hits even if
  FreeType reuses a font address. Renderer owns texture destruction, so Font
  does not retain renderer-specific pointers across renderer replacement.
- Basic and styled text reuse uploaded textures. Unrotated/unflipped texture
  draws use SDL_RenderCopy instead of SDL_RenderCopyEx. Other rotation/flip
  paths remain unchanged. Atlas pixels/hotspots/size are unchanged.

## Measurements

Isolated pipes only, never `/dev/input` or desktop input. Linker-wrapped read
accounting compares normal production batching with a test-only control that
limits each read to one input_event, reproducing the former syscall size.

| 128 mouse packets / 256 raw records | Single-record control | Production |
| --- | ---: | ---: |
| Parser read syscalls, including final EAGAIN | 257 | 5 |
| Unheld motion callbacks after main's batching helper | 128 without batching | 1 |

The read count is measured on both x86-64 and emulated MIPS. Final coordinates
remain x=448 from x=320; accumulated dx=128. MIPS input_event is 16 bytes;
host input_event is 24 bytes.

Host fixture: SDL dummy, software renderer, hidden window, logical AND actual
output 1280x720, 32 text draws/frame, supplied 11-sprite cursor atlas, five
warmup frames and 600 measured frames. The explicit uncached control reproduces
the original text rasterize/upload/draw/destroy path; it is not a separate
physical-device baseline. Texture-upload counts use linker wrapping.

| Section | Uncached median / p95 ms | Cached median / p95 ms |
| --- | ---: | ---: |
| Text API calls | 1.0417 / 2.9152 | 0.0091 / 0.0190 |
| Cursor API call | 0.0002 / 0.0018 | 0.0001 / 0.0002 |
| Present, including deferred SDL drawing | 0.0084 / 0.0377 | 0.8477 / 1.2635 |
| Whole fixture frame, excluding sleep | 1.0533 / 2.9510 | 0.8590 / 1.2803 |
| Measured text uploads | 19,200 | 0 after warmup |

SDL defers cached draws until present; uncached texture destruction forces
earlier flushing. Text-call timing alone therefore greatly overstates any
whole-frame gain. An earlier 360-frame pair measured whole-frame medians
1.1287 ms uncached and 1.1299 ms cached. Host contention varies, and software
blending remains dominant. No host/QEMU FPS is extrapolated to HC16xx.

## Verification

- Host cache: 573 checks, including exact uncached-reference pixel equality
  for basic UTF-8, RGBA/alpha, styled multiline alignment/spacing, rotation,
  clipping, reloads, renderer replacement, oversized text, bounded font and
  texture churn. Also passed with the dummy fullscreen output's non-1:1
  logical scaling, using actual renderer dimensions for readback.
- Host ASan + UBSan, leak detection enabled: cache and cursor-input tests
  passed (573 cache checks, 429 cursor-input checks including all three button
  drags). Existing remote-input test passed 31 checks; existing gamepad test
  passed 30 checks (keyboard, axes, remaps, disconnect, dropped packets,
  mouse/touchpad, and TV-safe coordinates).
- MIPS/musl input test passed under `qemu-mipsel -cpu 74Kf`; target event ABI
  and syscall counts verified (429 checks). The emulator's default CPU lacks the requested
  DSP instructions; selecting 74Kf avoids that irrelevant illegal instruction.
- `Font.cpp`, `Renderer.cpp`, and `main.cpp` compiled with target g++ and
  `-Wall -Wextra -Werror`, MIPS32r2/o32, `-mtune=74kc -mdspr2 -mfp32
  -mhard-float -mplt -mlong-calls`. Target text-cache test linked successfully.
- Target rendering test could not execute under user-mode QEMU: this target
  SDL build reports `dummy not available`. No target renderer runtime claim.
- Full image/UI integration build and physical operation are left to parent.

## On-Device Profiling

Enable `LILSMOKE_UI_PROFILE=1` for the native UI process. It logs one aggregate
every 120 rendered iterations, with actual measured averages:

`UI_PROFILE frames=120 events=... motion_dispatches=... sdl_event_ms=...
input_dispatch_ms=... render_ms=... cursor_ms=... present_ms=... frame_work_ms=...`

`render_ms` is view/keyboard render-command submission and includes text work;
`present_ms` includes deferred software drawing plus backend presentation.
`frame_work_ms` measures the complete iteration's work through present,
including SDL event pumping and input dispatch, but excludes the frame-cap
sleep. It is not FPS or isolated evdev read time. Profiling is off by default.

Measurement test builds enable `-DLILSMOKE_COUNT_READS -Wl,--wrap=read` for
`ui_cursor_input_smoke`, or `-DLILSMOKE_COUNT_UPLOADS
-Wl,--wrap=SDL_CreateTextureFromSurface` for `ui_cursor_perf`. Production UI
builds need neither flag. The pipe burst is selected with `--burst`; its
single-read control uses `LILSMOKE_TEST_SINGLE_READ=1`. The text fixture's
uncached control uses `LILSMOKE_PERF_UNCACHED=1`. Hidden/windowed host tests
use `SDL_VIDEODRIVER=dummy LILSMOKE_UI_HIDDEN=1 LILSMOKE_UI_WINDOWED=1`.

## Remaining Limits

- Full view redraw and full SDL_RenderPresent are still used. There is no
  existing view damage/invalidation contract, and SDL's normal render API does
  not promise retained backbuffer contents after present. Cursor-only dirty
  presentation was deliberately not enabled without a validated surface path.
- Large alpha artwork/software scaling, cold asset loads, font/layout work on
  cache misses, and periodic hotplug discovery can still cause real stalls.
- No physical cursor or end-to-end input-to-photon measurements yet. The
  changes and timing hooks are ready for the parent's native build/device pass.
