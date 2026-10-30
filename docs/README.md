# LilSmokeOS — Documentation Index

This is the entry point for all LilSmokeOS documentation. The docs are
organised by audience: if you just want to *use* the OS, start with the
User Guide. If you want to *build* it, see the Build Guide. If you want
to *extend* it, the Theme, Porting, and Contributor guides are below.

## Quick links

| You want to… | Read this |
|---|---|
| Just play games | [USER-GUIDE.md](USER-GUIDE.md) |
| Build from source | [../docs/build.md](build.md) |
| Run in QEMU (MIPS dev) | [../docs/build.md](build.md) + `scripts/run-qemu.sh` |
| Run in VirtualBox (x86 dev) | [vbox-guide.md](vbox-guide.md) |
| Make a theme | [THEME-GUIDE.md](THEME-GUIDE.md) |
| Use ArkOS / EmuELEC themes | [theme-compat.md](theme-compat.md) |
| Port to a new device | [PORTING-GUIDE.md](PORTING-GUIDE.md) |
| Understand the architecture | [../ARCHITECTURE.md](../ARCHITECTURE.md) |
| Contribute a PR | [../CONTRIBUTING.md](../CONTRIBUTING.md) |
| See what's new | [../CHANGELOG.md](../CHANGELOG.md) |
| Flash to a real SF3000/GB350 | [hardware.md](hardware.md) |
| Tune performance | [performance.md](performance.md) |
| Understand overclocking | [overclocking.md](overclocking.md) |
| Set up OTA updates | [ota.md](ota.md) + [wifi-ota.md](wifi-ota.md) |
| Add a libretro core | [cores.md](cores.md) |
| Add an OpenDingux emulator | [../package/opendingux-emulators/](../package/opendingux-emulators/) |
| Develop a LilSmoke Application | [Applications SDK/API](lilsmoke-applications-sdk.md) |
| Understand the UI internals | [frontend.md](frontend.md) |
| Wire up WiFi | [wifi-ota.md](wifi-ota.md) |
| Use USB/Wi-Fi SSH, LILDRIVE and mouse/touchpads | [remote-access.md](remote-access.md) |
| Review remote/input test evidence | [remote-input-validation.md](remote-input-validation.md) |
| Remap gamepad buttons | [input.md](input.md) |
| Manage files and archives | [lilsmoke-explorer.md](lilsmoke-explorer.md) |
| Package native PC ports | [portsmoke.md](portsmoke.md) |
| Understand the media stack | [media-stack.md](media-stack.md) |

## Document inventory

### Top-level

| File | Audience | Description |
|---|---|---|
| `../README.md` | Everyone | Project overview, build instructions, status table. |
| `../ARCHITECTURE.md` | Developers | Hardware constraints, design principles, layered architecture, memory budget, partition layout. **Mandatory reading before any PR.** |
| `../CONTRIBUTING.md` | Contributors | Code style, PR process, memory discipline, release checklist. |
| `../CHANGELOG.md` | Everyone | Per-release changes in Keep a Changelog format. |
| `../worklog.md` | Maintainers | Append-only chronological work record. |
| `../LICENSE_POLICY.md` | Everyone | Current original OS and per-component licensing; do not infer a single license for the whole image. |

### User-facing docs (this directory)

| File | Audience | Description |
|---|---|---|
| `USER-GUIDE.md` | End users | How to install, add ROMs, configure, play, update. |
| `THEME-GUIDE.md` | Theme authors | Theme XML format, palette, asset rules, EmuELEC/ArkOS compatibility. |
| `theme-compat.md` | Theme authors | EmulationStation theme-set loading, supported properties, and current compatibility gaps. |
| `PORTING-GUIDE.md` | Porters | How to bring LilSmokeOS to a new device. |
| `vbox-guide.md` | Developers | VirtualBox setup for fast x86_64 dev iteration. |
| `hardware.md` | Real-HW users | Flashing the SD card, UART debug, SF3000/GB350 specifics. |
| `performance.md` | Tuners | J7 optimization notes, measured before/after, tuning knobs. |
| `overclocking.md` | Tuners/porters | CPUFreq-based overclock setting, safety checks, and DTB requirements. |
| `lilsmoke-explorer.md` | End users | Built-in file manager controls, archive support, and limits. |
| `portsmoke.md` | Port authors | Native PC ports package layout, launcher flow, metadata, and game-script contract. |
| `hc16xx-research.md` | BSP developers | Pinned SF3000 evidence, corrected hardware contract, adopted work, and blockers. |

### Developer docs (this directory)

| File | Audience | Description |
|---|---|---|
| `build.md` | Builders | Host prerequisites, first build, rebuild tips, troubleshooting. |
| `lilsmoke-applications-sdk.md` | Application developers | SDK CLI, software SDL2 API, preview/build/package workflow and runtime prerequisites. |
| `media-stack.md` | Developers | DirectFB + SDL2 + ALSA + libevdev + fonts. Perf budget. |
| `cores.md` | Developers | RetroArch + 8 libretro cores + PCSX4ALL. Core matrix. |
| `frontend.md` | Developers | LilSmokeUI architecture, view stack, theme loader, launch flow. |
| `input.md` | Developers | Keymap file format, file locations, UI remapping. |
| `ota.md` | Developers | A/B partition layout, boot flow, OTA bundle format, rollback. |
| `wifi-ota.md` | Developers | WiFi bring-up, Screenscraper.fr scraper, OTA server deployment. |

## Audience cheat sheet

### "I just want to play games on my SF3000"

1. Read `USER-GUIDE.md` → flash the image, add ROMs.
2. If something doesn't work, check `hardware.md` for the debug UART.

### "I want to develop LilSmokeOS"

1. Read `../ARCHITECTURE.md` end to end.
2. Read `build.md` and `vbox-guide.md` — VirtualBox is the fastest
   dev loop (x86_64, no MIPS emulation overhead).
3. Pick a job from `../worklog.md` that's marked ⏳ pending, or open an
   issue describing what you want to work on.
4. Read `../CONTRIBUTING.md` before sending a PR.

### "I want to write a theme"

1. Read `THEME-GUIDE.md` for the format.
2. Drop your theme folder into `/storage/themes/<name>/`.
3. Pick it in LilSmokeUI → Settings → Theme.

### "I want to port LilSmokeOS to a new handheld"

1. Read `PORTING-GUIDE.md` for the board bring-up workflow.
2. Skim `../ARCHITECTURE.md` to understand the constraints your target
   needs to meet (no GPU, framebuffer, MIPS or x86_64, ≤ 256 MB RAM).
3. Use `board/vbox-x86_64/` as the simplest reference board (x86_64 +
   VESA fbdev + AC97 audio + E1000 net — all available in any VM).

### "I want to flash to real SF3000/GB350 hardware"

1. Read `hardware.md` for the SD card flashing procedure.
2. Read `ota.md` to understand A/B partitions and rollback.
3. Read `wifi-ota.md` to set up OTA updates once you're running.

### "I want to make it faster"

1. Read `performance.md` for the J7 tuning notes.
2. The big knobs are: `CPU_GOVERNOR`, `ASSET_CACHE_MB`, `ZRAM_SIZE_MB`,
   the `LILSMOKE_LITE` preset, and `BR2_OPTIMIZE_3` + `BR2_ENABLE_LTO`.

## Doc conventions

- Markdown only. No reStructuredText, no AsciiDoc.
- 80-char wrap where reasonable.
- No emojis.
- Code blocks use ```sh, ```c, ```cpp, ```bash, ```makefile, ```text
  fences (not bare ```).
- File paths are relative to repo root unless stated otherwise.
- Cross-doc links use relative paths from the linking doc.
- Screenshots are NOT committed to the repo — link to an external host
  if you need one.

## Updating this index

If you add a new doc, add a row to the relevant table above and a row
to the Quick Links table. A maintainer will catch it in review if you
forget, but it's polite to do it yourself.
