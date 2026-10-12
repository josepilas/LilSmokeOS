# Changelog

## Pre-Alpha-0.0.1 - Draft, Unreleased

Prepared on 2026-10-12 as the intended first public release documentation.
No GitHub release, sanitized public image or release checksum is declared here.
Older version/J entries below are preserved historical development notes; their
"shipped" language, board assumptions and measurements are not certified public
release history. Current evidence is summarized in the new release guides.

### Documentation

- Added installation/update, stock import, user manual, emulator/content,
  application, gamepad, theme, media, network and troubleshooting guides.
- Added source/build, board/profile porting, `.lilpck` and testing guides.
- Added credits naming José Pilas as creator, designer and project lead and
  acknowledging upstream projects and hardware research.
- Updated contribution guidance and added structured GitHub issue templates.
- Documented MPL-2.0 policy for original LilSmoke work without overriding
  third-party terms, vendor-firmware gates or SM64 distribution restrictions.

### Development Scope To Audit

- Physically tested HCSEMI M8 UI/emulation/input/shutdown baseline.
- Bundled LilSmoke Applications, PCSX4ALL emulator-app integration and native
  OpenDingux-derived source ports; final provider inventory requires review.
- Explorer media handoff and portable stock-folder Device Adapter, with explicit
  physical-media/macOS/new-board test gaps.
- Original-engine SM64 work remains private; asset-bearing builds and the
  source-only graphics backend are excluded from public release artifacts.

See [release notes](docs/pre-alpha-0.0.1/RELEASE-NOTES.md) and the
[publication gate](docs/pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md).

---

All notable changes to LilSmokeOS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
for its release tags. Internal milestones are numbered `J0` through `J11`
(see `worklog.md` for the full chronological record).

Each `J` job is shipped as a point release (`0.1.0` = J0, `0.2.0` = J1+J2,
etc.). The "Unreleased" section at the top collects changes that will land
in the next tagged release.

## [Unreleased]

### Pending
- J10: theme editor UI (in-progress)
- J11: multi-device sync via SD card swap

---

## [0.7.0] — J9 (Documentation polish)

### Added
- Complete documentation suite: `docs/README.md`, `docs/USER-GUIDE.md`,
  `docs/THEME-GUIDE.md`, `docs/PORTING-GUIDE.md`, `docs/vbox-guide.md`,
  `docs/performance.md`, `docs/hardware.md`.
- `CONTRIBUTING.md` — contributor handbook with code style, PR process,
  memory discipline rules, and release checklist.
- `CHANGELOG.md` (this file) in Keep a Changelog format covering J0–J11.
- `LICENSE` — MIT, with explicit per-component license table.
- Top-level `docs/README.md` index of all documentation.

### Changed
- `README.md` updated with the full J0–J11 status table and links to
  every doc.

---

## [0.6.0] — J8 (Real hardware image)

### Added
- `configs/lilsmoke_sf3000_defconfig` — real HW build for the SF3000
  (Ingenic JZ4770, U-Boot, FIT image, A/B partitions).
- `configs/lilsmoke_gb350_defconfig` — same SoC, GB350 3.5″ panel.
- `board/sf3000/linux.config` — Ingenic JZ4770 kernel config with
  IPS-panel framebuffer, GPIO keypad, JZ MMC, JZ4770 IPU (no GPU).
- `board/sf3000/uboot-env.txt` — U-Boot environment with A/B slot
  selection, bootcounter rollback, and `lilsmoke.slot=` cmdline arg.
- `board/sf3000/input-maps/sf3000.map` — GPIO keypad evdev codes for
  the stock SF3000 d-pad + ABXY + L/R + Start/Select.
- `board/sf3000/post-build.sh` and `post-image.sh` — installs SF3000-
  specific files and packs a FIT image (`kernel + dtb + initramfs`).
- `board/gb350/` — same as sf3000 but with GB350 dtb and panel config.
- `scripts/mk-sdcard.sh` — SD card image builder. Produces a flashable
  `.img` with the A/B partition layout from `docs/ota.md`.
- `docs/hardware.md` — real-HW bring-up guide: flashing, partition
  layout, boot flow, debugging over UART, known-good SD cards.

### Changed
- `external.mk` now selects the right `LILSMOKE_BOARD_DIR` for the
  SF3000 and GB350 targets.
- `board/qemu-mipsel/init.d/S05bootcounter` — added a comment that on
  real HW the `reboot` call is uncommented to force slot fallback.
- `board/qemu-mipsel/post-image.sh` — writes a more complete version
  manifest including the active slot and build target.

---

## [0.5.0] — J7 (Performance pass)

### Added
- `docs/performance.md` — J7 optimization notes: `-O3` + LTO flags,
  MIPS DSP ASE usage in hot loops, profile-guided optimization, zram
  tuning, DirectFB patch list, measured before/after numbers.
- `package/directfb-mips/patches/0001-fix-sigval_t-musl.patch` —
  fixes DirectFB's `sigval_t` usage which doesn't exist under musl
  (musl uses `union sigval`). Wired in via a `POST_PATCH` hook in
  `external.mk`.
- `-O3` + `-flto` flags added to the LilSmokeUI and RetroArch builds.
- PCSX4ALL now built with `-DRKGAME_FRAME_WIDTH=320` and a 4-times-per-
  frame SPU update (was 1) — eliminates audio dropouts in Gran Turismo.
- zram algorithm switched from LZ4 to zstd (better ratio, ~5% CPU cost
  on 74Kf, net win for 256 MB devices).
- Asset cache LRU added to LilSmokeUI — cover art evicted in true LRU
  order rather than insertion order.

### Changed
- Kernel `CONFIG_HZ` bumped from 100 to 250 on real HW (QEMU stays at
  100) — reduces input-to-frame latency from 10 ms to 4 ms.
- `board/qemu-mipsel/init.d/S10zram` — bumped zram size from 24 MB to
  32 MB based on J7 measurements.
- Default `CPU_GOVERNOR` is now `ondemand` (was `performance`) — saves
  ~150 mW idle on real HW with negligible game-speed impact.

### Measured impact
- LilSmokeUI idle RSS: 22 MB → 14 MB (-36%)
- LilSmokeUI stripped binary: 920 KB → 712 KB (-23%)
- Rootfs squashfs: 28 MB → 24 MB (-14%)
- Boot time to UI (QEMU): 8.2 s → 5.1 s (-38%)
- Boot time to UI (SF3000): 11.4 s → 7.8 s (-32%)

---

## [0.4.0] — J6 (WiFi + scraping + OTA server)

### Added
- `package/lilsmoke-scripts/src/lilsmoke-scrape` — real Screenscraper.fr
  scraper. Supports `--all`, single-ROM, and per-system modes. Caches
  metadata in `/persist/scrape/<hash>.json` and box art in
  `/persist/scrape/covers/<system>/<rom>.png`. Rate-limited to 1 req/s.
- `tools/ota-server/ota-server.py` — reference OTA server (Python 3,
  no external deps). Subcommands: `serve`, `publish`, `manifest`.
  Endpoints: `GET /manifest.json`, `GET /lilsmoke-update-<v>.tar.gz`.
- `package/lilsmoke-ui/src/src/OTAView.{h,cpp}` — UI for the OTA
  updater. Shows current version, slot status, available updates, and
  Check/Download/Install/Activate/Reboot buttons.
- `docs/wifi-ota.md` — WiFi architecture, supported USB dongles table
  (RTL8188FU / RTL8189FS / 8723BS / 8723DS / ESP8089), Screenscraper.fr
  setup, OTA server deployment guide.

### Changed
- `package/lilsmoke-scripts/src/lilsmoke-update` — rewrote the stub
  into a real A/B OTA updater with `status`, `check`, `download`,
  `install`, `activate`, `reboot` subcommands. Verifies SHA256 against
  the manifest before installing.
- `package/lilsmoke-ui/src/src/SettingsView.cpp` — added "OTA Update"
  submenu entry that pushes an `OTAView` onto the view stack.

---

## [0.3.0] — J5 (Input system + keymap loader)

### Added
- `package/lilsmoke-ui/src/src/Keymap.{h,cpp}` — file-based keymap
  with `loadDir()`, `loadFile()`, `lookup()`, `set()`, `saveUserMap()`.
  Map file format: `evdev <code> <action>` per line, `#` comments.
- `package/lilsmoke-ui/src/src/InputSettingsView.{h,cpp}` — UI for
  remapping buttons. 15 mappable buttons, swap-existing-binding model.
- `board/qemu-mipsel/input-maps/default.map` — default keymap for the
  SF3000/GB350 GPIO keypad (with placeholder codes pending SF3000-RE
  decompilation) plus QEMU dev keyboard bindings.
- `docs/input.md` — keymap file format, file locations, UI remapping
  instructions, manual editing, default keymap table.

### Changed
- `package/lilsmoke-ui/src/src/Input.{h,cpp}` — replaced the hardcoded
  `mapKey()` with a `Keymap` member loaded from `/etc/lilsmoke/input-maps/`
  and `/persist/config/input-maps/`. Falls back to hardcoded defaults
  if no keymap files exist (so QEMU dev still works out of the box).
- `package/lilsmoke-ui/src/src/SettingsView.cpp` — added "Input —
  Button Mapping" submenu entry.
- `package/lilsmoke-ui/src/Makefile` — added `Keymap.cpp` and
  `InputSettingsView.cpp`.

---

## [0.2.0] — J3 + J4 (LilSmokeUI v0.1 + A/B persistence)

### Added
- `package/lilsmoke-ui/src/` — real C++17/SDL2/DirectFB frontend
  (~1500 LOC across 11 source files):
  - `main.cpp` — App class, main loop, view stack.
  - `Log.{h,cpp}` — stderr logger with timestamps.
  - `Config.{h,cpp}` — reads/writes `/persist/config/lilsmoke.conf`.
  - `Theme.{h,cpp}` — XML theme loader (EmuELEC/ArkOS subset).
  - `ROMScanner.{h,cpp}` — walks `/storage/roms/<system>/`, knows file
    extensions for 12 systems.
  - `Renderer.{h,cpp}` — SDL2 wrapper, software renderer, PNG cache.
  - `Font.{h,cpp}` — SDL2_ttf wrapper, DejaVu Sans @ 16pt default.
  - `ListView.{h,cpp}` — vertical scrolling list (SystemList root).
  - `GridView.{h,cpp}` — 4×3 cover-art grid (game selection).
  - `SettingsView.{h,cpp}` — 9-row settings menu.
  - `Launcher.{h,cpp}` — `fork()` + `execve(lilsmoke-launch)`.
- `board/qemu-mipsel/init.d/S05bootcounter` — A/B boot counter with
  rollback after 3 failed boots.
- `board/qemu-mipsel/init.d/S06overlay` — OverlayFS for `/etc` and
  `/var` (lower = read-only squashfs, upper = `/persist/overlay/*`).
- `board/qemu-mipsel/init.d/S89bootok` — marks current slot as good
  after a 5-second delay post-UI-start.
- `docs/ota.md` — full A/B partition spec, boot flow diagram, OTA
  bundle format, rollback scenarios.
- `docs/frontend.md` — LilSmokeUI architecture, theme compatibility
  matrix, launch flow, input mapping table.

### Changed
- `package/lilsmoke-ui/lilsmoke-ui.mk` — now actually compiles the
  C++ source (was a J0 stub). Uses `TARGET_CXX` with `-std=c++17 -O2
  -ffunction-sections -fdata-sections -Wl,--gc-sections -s`.
- `board/qemu-mipsel/init.d/S01mounts` — mounts `/persist` (tries
  `mmcblk0p4` → `sda4` → tmpfs fallback) and `/storage` (tries
  `mmcblk0p5` → `sda5` → tmpfs fallback).
- `board/qemu-mipsel/init.d/S90lilsmoke-ui` — creates `/persist/bios`,
  `/persist/saves`, `/persist/states`, `/persist/scrape/covers`,
  `/storage/roms` on first boot.
- `package/lilsmoke-scripts/src/lilsmoke-update` — first version of
  the A/B OTA updater (rewritten again in J6).

---

## [0.1.0] — J0 + J1 + J2 (Skeleton + media stack + emulation)

### Added — J0 (Buildroot skeleton)
- Repo skeleton at the project root: `README.md`, `ARCHITECTURE.md`,
  `Config.in`, `external.mk`, `external.desc`.
- `configs/lilsmoke_qemu_mipsel_defconfig` — musl, 74Kf, DSP ASE, hard
  float, squashfs + cpio initramfs.
- `board/qemu-mipsel/` — `linux.config` (minimal MIPS kernel with
  fbdev, ALSA, USB HID, zram, squashfs, overlayfs), `busybox.config`,
  `inittab`, `lilsmoke.conf`, `directfbrc`, `theme.xml`,
  `input-maps/default.map`, `mdev/hotplug-{sd,js}.sh`, `post-build.sh`,
  `post-image.sh`.
- 8 init scripts: `S01mounts`, `S02mdev`, `S03hostname`, `S10zram`,
  `S20network`, `S30dropbear`, `S40alsa`, `S90lilsmoke-ui`.
- Package stubs: `lilsmoke-scripts` (with real `lilsmoke-launch`,
  `lilsmoke-wifi`, `lilsmoke-scrape`, `lilsmoke-update` helpers),
  `directfb-mips`, `lilsmoke-ui`, `retroarch-mips`.
- `scripts/build.sh` — Buildroot wrapper with auto-clone.
- `scripts/run-qemu.sh` — QEMU-mipsel malta launcher.
- `docs/build.md` — prerequisites + first-build walkthrough.
- `assets/logo/lilsmoke-logo.webp` — official LilSmokeOS logo.

### Added — J1 (Media stack)
- `package/directfb-mips/` — meta-package that pulls in Buildroot's
  stock DirectFB with the LilSmokeOS fbdev-only profile.
- `package/lilsmoke-fbtest/` — tiny C smoke test (~12 KB stripped)
  that mmaps `/dev/fb0` and paints the LilSmokeOS logo via libpng.
  Falls back to a procedurally-drawn "L" if PNG is missing.
- `board/qemu-mipsel/init.d/S50fbtest` — runs the smoke test for 10
  seconds between ALSA setup and LilSmokeUI startup.
- Updated `configs/lilsmoke_qemu_mipsel_defconfig` to enable SDL2 +
  DirectFB backend + ALSA + libevdev + libpng + libjpeg-turbo +
  FreeType + DejaVu fonts.
- `docs/media-stack.md` — full architecture diagram, perf budget,
  verification steps.

### Added — J2 (RetroArch + cores + PCSX4ALL)
- `package/retroarch-mips/` — builds RetroArch v1.19.1 with DirectFB
  video, ALSA audio, evdev input, no OpenGL/GLES/Vulkan/X11/Wayland/KMS.
  Cross-compiles 8 libretro cores in parallel:
  - FCEUmm (NES)
  - Gambatte (GB/C)
  - mGBA (GBA)
  - Snes9x 2005 (SNES)
  - Genesis Plus GX (MD/SMS/GG)
  - FBNeo (arcade)
  - Beetle PCE Fast (PCE)
  - Handy (Lynx)
- `package/retroarch-mips/retroarch.cfg` — LilSmokeOS-tuned defaults
  (DirectFB video, ALSA audio, evdev input, RGUI menu, integer scale,
  4:3 aspect, no smoothing).
- `package/pcsx4all/` — uses pre-built `libpcsx4all_rkgame.a` (MIPS-to-
  MIPS dynarec, 4 MB) and builds a tiny C launcher that calls
  `PCSX_Load()` + `pcsx_Thread()` via `rkgame_bridge.h`.
- `package/pcsx4all/src/pcsx4all-launch.c` — 150-line launcher.
- `package/pcsx4all/pcsx4all.cfg` — runtime config (BIOS path, save
  dirs, `gpu_unai`, `spu_pcsxrearmed`, `dynarec=1`, `fast_boot=1`).
- `package/lilsmoke-scripts/src/lilsmoke-launch` — routes PS1 to
  `pcsx4all-launch` and everything else to RetroArch with the correct
  core.
- `docs/cores.md` — full core matrix, RetroArch config table, PCSX4ALL
  architecture diagram, ROM formats, performance tuning.

### Changed
- `ARCHITECTURE.md` — updated to reflect PS1 is now Excellent (via
  PCSX4ALL), only N64/PSP/DC/PS2 remain unsupported.

---

## Versioning policy

LilSmokeOS uses `<major>.<minor>.<patch>`:

- **major** — will stay `0` until 1.0 (first release that boots on real
  SF3000 hardware end-to-end with OTA updates working).
- **minor** — bumped per J-job milestone (J0→0.1, J1+J2→0.2, etc.).
- **patch** — bugfix releases between milestones (e.g. `0.5.1`).

Internal version strings shipped on the device (e.g. `0.1.0-j0` in
`/etc/lilsmoke/lilsmoke.conf`) include the J-job tag for traceability.

## Link conventions

Each `[x.y.z]` heading is a Markdown reference link. When tagging a
release, replace the `(Unreleased)` style heading date with the actual
release date in ISO 8601 (`YYYY-MM-DD`).
