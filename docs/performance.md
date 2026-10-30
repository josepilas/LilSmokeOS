# Emulation Performance

## Evidence Boundary

The historical J7 before/after tables, physical-console FPS, battery-life,
boot-time, memory, and compiler speedup claims previously in this file had no
reproducible evidence attached. They are withdrawn, not current measurements.
Integer enlargement still costs CPU; it is not free scaling. Static libretro
linking is possible and does not inherently violate the libretro API.

The SF3000 target is currently documented as one HC16xx/MIPS 74Kc with 256 MiB
RAM, not a dual-core Ingenic. See [hardware.md](hardware.md) for the unresolved
display/audio/BSP constraints. Do not flash these test images to a console.

The final functional tests use Malta, one MIPS vCPU, 128 MiB RAM, musl, BusyBox,
SDL2/DirectFB software rendering and ALSA. Its Cirrus display is 856x480 RGB24;
the physical display contract is different. QEMU TCG is not cycle accurate and
does not simulate an accurately calibrated 1188 MHz console. VM FPS and WebM
capture FPS must never be presented as physical-console performance.
The optional `74Kf-lilsmoke` QEMU profile matches the researched CP0 TLB/cache
geometry more closely than stock `74Kf`; it still is not an HC16xx machine.
The existing Windows emulator EXE has not been repackaged by this work.

## Verified Problems And Changes

- The old SNES prebuilt forces `FAST_LSB_WORD_ACCESS`. It faults with SIGBUS
  under MIPS user-mode emulation. Linux system emulation can repair such
  unaligned accesses, concealing the defect with additional work. The new
  source build respects the core's MIPS alignment-safe implementation.
- The package previously installed that prebuilt after source compilation,
  overwriting the rebuilt core. Installation now uses the selected, pinned
  target builds only, with per-core provenance and ELF checks.
- Cores previously tracked moving branches. `package/retroarch-mips/cores.lock`
  pins source revisions and canonical filenames. Target optimization flags
  are applied after upstream defaults without removing required PIC flags.
- RetroArch 1.19.1 keeps the first duplicate configuration key. The launcher
  previously appended mandatory settings, which could therefore be ignored.
  Mandatory settings now come first, followed by user preferences and base
  defaults, with duplicate keys removed.
- Rewind, runahead, preemptive frames, shaders, frame delay, black-frame
  insertion and audio DSP filters are disabled in the constrained profile.
  Audio and normal game timing remain enabled. New SNES options files default
  to rendering every frame; existing user frameskip choices are preserved.
- The RetroArch backend now owns non-PS1 archive extraction. Cache identity
  includes the canonical archive path, avoiding collisions between ordinary
  same-named files in different folders.
- LilSmokeUI queues owned launch arguments, tears down App/SDL/DirectFB, then
  hands off with `execve`. Theme drawing is not running alongside RetroArch.
  Held Confirm cannot leak through the system/game-list transition; fresh
  presses launch, while directional repeat still works.
- A 128 MiB VM boot ran out of memory with hundreds of kernel-launched mdev
  helpers, before the benchmark started. The kernel helper path is now empty;
  S02 no longer enables it alongside Buildroot's S10 netlink daemon. The
  rebuilt kernel passed normal 128 MiB UI boot and controlled comparisons.
  Its SHA256 is `31e5bdc16d762c34b2b96f7600b09d68599106b8cc054a561ac8fbaabb2b56ff`.
- A separate SDL2 viewport-readback patch avoids copying the full window into
  a viewport-sized screenshot buffer and leaking a converted surface.
- Native-format software textures convert at core resolution before enlargement.
  Repeated nearest-neighbor output rows reuse the previous row via `memcpy`,
  without skipping game frames, reducing colors or adding a frame-sized cache.
  Overlapping memory spans use the original scaling path.
- DirectFB's legacy console keyboard consumed escape bytes as game keys.
  SDL2 now enumerates keyboard capabilities safely; DirectFB forces evdev and
  disables the legacy keyboard module. RetroArch uses SDL2 keyboard input and
  linuxraw joypads. Space/Select no longer trigger fast-forward/exit.
- Evdev alone does not stop Linux's console shortcuts. DirectFB must own raw
  mode on the current VT (`vt`, `no-vt-switch`, `no-vt-switching`). The former
  `no-vt` setting allowed Alt+Right to switch away from a running game.
- Pinned `.info` files now advertise installed cores under their binary names.
  Their absence disabled RetroArch's save-state menu. SNES states are marked
  basic, not deterministic: video restores, but audio is not bit-exact.
- Stock QEMU 8.2.2 `74Kf` models 16 TLB entries and 2 KiB caches. The researched
  CP0 dump reports 32 TLB entries and 32 KiB caches. The optional profile also
  needs Config7 WAIT-interrupt behavior to boot Linux reliably. Cache sizes are
  not cycle-timed; most of this improvement corrects the VM, not the console.

## Controlled Measurements

Exploratory measurements are in `test-results/emulation-performance/` and the
continuation ledger is [emulation-performance-progress.md](emulation-performance-progress.md).
Do not compare runs with different frame counts, input sequences, instrumentation,
states or host load as a controlled speedup.

The original 1200-frame VM diagnostic, without screen recording or frameskip,
reported 6.027 process-average FPS for SDL software video and 6.331 with ALSA.
These include process startup. They reproduce the slowdown without a themed UI
or video encoder, but do not establish how the physical device performs.

A 300-frame verification run with the pinned SNES core produced identical
video (`49515316`) and audio (`968746c8`) hashes in the diagnostic host and
RetroArch. This verifies that particular boot/title sequence, not every game
or gameplay state. Raw save-state hashes are not comparable across processes:
this older core serializes pointer-bearing structures.

Under the corrected low-memory kernel, 600-frame process-average throughput
was 9.325 FPS with stock `74Kf`, 25.168 with the closer CPU profile, and 27.113
with that profile plus the initial native-format graphics changes. This single
comparison is not a repeatable universal speedup or a physical-console result.

Two subsequent suites alternated baseline/candidate order, twice each, with
600 boot/title frames, identical MIPS core, one vCPU, 128 MiB, DirectFB software
video and ALSA. No encoder or build ran during measurement. Hash verification
adds overhead equally to both variants. All 24 cases exited successfully,
rendered 600 frames with zero duplicates, and matched video `b11bb1c2` and
audio `b725c285`.

Final suite, including repeated-row reuse:

| Case | Baseline FPS samples | Candidate FPS samples | Median change |
|---|---|---|---|
| Software + ALSA, null input | 23.791, 23.392 | 23.229, 21.591 | -5.01% |
| Software + ALSA + SDL input | 22.140, 24.370 | 22.590, 23.622 | -0.64% |
| Above + font/FPS overlay | 20.121, 24.106 | 23.791, 24.702 | +9.65% |

The earlier native-format-only suite reported +14.78%, -2.30%, and +6.59%
for those cases respectively. Samples overlap and vary; two repetitions are
not enough to establish a statistically reliable overall speedup. The final
input/overlay case has median 24.247 FPS versus 22.114, not 60 FPS.

Repeated-row scaling matches 36 baseline image hashes exactly, covering
INDEX8/RGB565/RGB24/ARGB8888, up/down/identity scaling, cropped destinations,
odd pitch and overlapping memory. Isolated MIPS user-mode scaling over 100
calls reduced elapsed time by about 40% (RGB565), 39% (RGB24) and 28% (ARGB8888).
Those microbenchmarks are not whole-game or physical-console measurements.

Final full-RetroArch peak process RSS was about 29 MiB without the font overlay
and 37.2 MiB with it. Core-only peak RSS was 12.2 MiB. These are process RSS,
not total guest RAM, and do not certify other games or cores fit 128 MiB.

Most measured time is still in video/frontend/VM work, not input callbacks.
Core-only throughput cannot be used as a gameplay FPS claim. Audio remains
synchronized at 64 ms latency; lower resampler quality or muted audio was not
adopted to disguise the slowdown. Stock clocks, every rendered frame and normal
game timing are preserved. **The full-speed performance goal remains unmet.**

## Functional And Correctness Checks

Normal BusyBox boot and LilSmokeUI selection launched the supplied SNES `.7z`
through the production router and extracted the `.sfc` to its path-keyed cache.
Recorded sessions show game menus, a playable level, movement/jumping, Start
pause/resume, a RetroArch menu, an F8 screenshot, and basic save/load. The
metadata retest created/loaded a 529168-byte uncompressed state and saved SRAM.
The final recording's RZIP container occupies 90028 bytes on disk and validates
to that uncompressed size. Start pause was verified by identical gameplay-region
captures at separated times; the active Linux terminal remained `tty1`.
F12 exit has
RetroArch's normal two-press confirmation; tests must resume out of the menu
before treating the gameplay exit hotkey as an exit test.

The strict core diagnostic deliberately remains failing (`CORE_EXIT=7`):
restoring a state reproduces video, but the serializer omits sound history and
fractional sample phase, producing different audio/sample counts after load.
It also verifies Start changes the resumed video. Do not label this an
all-green deterministic-save test. Fixing the legacy serializer requires a
versioned state-format change and broader regression coverage.

Focused regressions: hardware contracts 43; launcher/config/archive 39; MIPS
software pixels/readback 32; exact scaling equivalence 36; core loading/API 8;
host UI 46; input 21; USB 29; network 8. The host service checks mock hardware;
the MIPS pixel/API checks use user-mode QEMU, not physical display drivers.
Production patches reproduce the tested source fixtures when applied cleanly.
The final recorded session passed 18 additional workflow/VT/persistence checks.

A separate dummy-display isolation attempt failed video initialization before
running any frames: that backend is unavailable in the target SDL build. Its
initial requested-frames/startup-time value is invalid and must not be used as
an FPS measurement. The harness now reports `process_fps: null` on failed
startup or an incomplete workload; an expected-failure retest confirmed zero
completed frames and no published throughput. This diagnostic does not
establish Cirrus as the sole cause of the remaining video cost.

Evidence is exported to `test-results/emulation-performance/final-evidence/`.
Manifests, uncut silent WebM recordings, screenshots, logs, paired results,
kernel and QA initramfs are retained. No ROM or save data is exported there.
The QA initramfs includes the tested SNES stack, not a complete eight-core
release image. It is a Malta QA image, not console firmware or a Windows EXE.
It retains the functional VT trace. All 1045 base/overlay archive entries were
checked for embedded game assets; none of the checked ROM/asset types were found.

## Reproduction

Run under Linux/WSL with the project's built MIPS toolchain, kernel and rootfs.
Use a fresh output directory per run; these tests do not touch normal saves.

```sh
python3 tools/hwtest/emulation_config_smoke.py
sh tools/hwtest/build_pinned_cores.sh snes9x2005
python3 tools/hwtest/emulation_benchmark.py \
  --rom /path/to/legal/game.sfc --out /path/to/fresh-results \
  --core test-results/emulation-performance/pinned-cores/snes9x_2005_libretro.so \
  --frames 600 --cases core,software-alsa --verify
sh tools/qemu/build-profile.sh
python3 tools/hwtest/run_performance_suite.py \
  --rom /path/to/legal/game.sfc \
  --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
  --out /path/to/fresh-comparison --repeats 2
python3 tools/hwtest/final_emulation_checks.py --out /path/to/fresh-checks
```

`--verify` hashes every core video/audio sample and adds diagnostic overhead.
`--profile` adds SDL timing and explicit renderer flushes, also perturbing
timing. `--direct-core` skips the callback proxy. Use the same instrumentation
for comparisons and repeat without instrumentation for final throughput.
The manifest records the ROM/core/frontend hashes, VM arguments and test mode.
`process_fps` includes startup; `core_profile.fps` excludes load/startup.
Neither is the number of unique encoded video frames per second.

For hardware tests record board revision, firmware/core hashes, ROM hash,
governor and actual frequency, RAM/swap usage, temperature, game region, audio
underruns, frames rendered/skipped and emulation speed over the same gameplay
segment. A 30/60 FPS WebM container alone proves none of those measurements.

## Conservative Defaults

Keep software rendering, nearest-neighbor scaling, audio synchronization and
stock clocks. Do not enable overclocking without supported CPUFreq states and
validated thermal telemetry. ZRAM is optional memory-pressure management, not
a way to increase CPU emulation throughput. No automatic clock increase or
physical PLL write is part of this performance work.

Avoid enabling frameskip merely to inflate an FPS display. If a user chooses
it, report emulation speed separately from rendered frames and audio quality.
Do not sacrifice input handling, save persistence or game timing for a higher
benchmark number.

## Primary References

- [Linux: unaligned memory access](https://docs.kernel.org/core-api/unaligned-memory-access.html)
- [Pinned SNES MIPS alignment definitions](https://github.com/libretro/snes9x2005/blob/a79dfe9047e7fec58808aefe48ad2bf499c7af11/source/port.h)
- [RetroArch 1.19.1 configuration parser](https://github.com/libretro/RetroArch/blob/v1.19.1/libretro-common/file/config_file.c)
- [SDL 2.28.5 software renderer](https://github.com/libsdl-org/SDL/blob/release-2.28.5/src/render/software/SDL_render_sw.c)
- [SDL nearest-neighbor stretch implementation](https://github.com/libsdl-org/SDL/blob/release-2.28.5/src/video/SDL_stretch.c)
- [DirectFB terminal ownership and keyboard mode](https://github.com/DirectFB/directfb/blob/master/systems/fbdev/vt.c)
- [SF3000 CP0 register dump](https://github.com/SiarheiVolkau/sf3k-hacking/blob/main/cpuinfo.dump)
- [Pinned core capability metadata](https://github.com/libretro/libretro-core-info/tree/5a74858ab2f7a50cebb5a6330895bc38899531c0)
- [QEMU instruction counting is not cycle-accurate emulation](https://www.qemu.org/docs/master/devel/tcg-icount.html)
- [Linux uevent-helper memory-pressure warning](https://github.com/torvalds/linux/blob/v5.15/drivers/base/Kconfig)
- [BusyBox mdev daemon and helper modes](https://github.com/mirror/busybox/blob/1_36_1/util-linux/mdev.c)
