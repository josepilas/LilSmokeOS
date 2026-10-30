# LilSmokeOS — Emulation Cores (J2)

This document describes the J2 emulation layer: RetroArch + 8 libretro
cores + PCSX4ALL (PS1 via MIPS dynarec).

## Configured Systems

These are configured integrations, not physical-console speed or game
compatibility guarantees. Earlier labels such as "Excellent", "full speed",
"all mappers" and "60 FPS on most titles" were not supported by recorded
measurements and are withdrawn. See [performance.md](performance.md).

| System | Status | Emulator | Notes |
|---|---|---|---|
| NES, Famicom, FDS | Configured | FCEUmm (libretro) | Game/mappers require testing |
| Game Boy (DMG) | Configured | Gambatte (libretro) | Physical timing unmeasured |
| Game Boy Color | Configured | Gambatte (libretro) | Same core, dual mode |
| Game Boy Advance | Configured | mGBA (libretro) | No measured full-speed guarantee |
| SNES / Super Famicom | VM tested | Snes9x 2005 (libretro) | Slow VM throughput under investigation |
| Genesis / Mega Drive | Configured | Genesis Plus GX (libretro) | Software rendering |
| Master System | Configured | Genesis Plus GX (libretro) | Same core |
| Game Gear | Configured | Genesis Plus GX (libretro) | Same core |
| PC Engine / TG16 | Configured | Beetle PCE Fast (libretro) | BIOS/assets required for CD |
| Arcade (CPS1, early CPS2, early Neo Geo) | Configured | FBNeo (libretro) | Per-game memory limits apply |
| Atari Lynx | Configured | Handy (libretro) | Requires game validation |
| **PS1 / PlayStation** | **Experimental** | **PCSX4ALL (rkgame reconstruction)** | **MIPS dynarec needs physical validation** |
| N64, PSP, Dreamcast, PS2 | **Not supported** | — | Hardware cannot |

## RetroArch configuration

RetroArch is configured for fbdev-only rendering (no OpenGL/GLES):

| Setting | Value | Why |
|---|---|---|
| `video_driver` | `sdl2` | SDL2 → DirectFB → fbdev |
| `audio_driver` | `alsa` | Direct ALSA, no dmix |
| `input_driver` | `sdl2` | DirectFB evdev keyboard; avoids competing tty readers |
| `input_joypad_driver` | `linuxraw` | Joystick device backend, not console keyboard input |
| `menu_driver` | `rgui` | Lightweight software menu selected for this build |
| `video_fullscreen` | `true` | Always fullscreen (no window manager) |
| `video_scale_integer` | `true` | Pixel-perfect on 640x480 |
| `video_aspect_ratio` | `1.3333` | 4:3 native |
| `video_smooth` | `false` | No bilinear filtering (sharp pixels) |

Default config file: `/etc/retroarch.cfg` (installed by the `retroarch-mips` package).

Pinned core info files install in `/usr/share/libretro/info`, renamed to match
each installed binary. Without them RetroArch 1.19.1 disables save states.
Metadata caching is disabled for these eight small files to avoid carrying
old missing-info capabilities through an upgrade. Snes9x 2005 exposes basic
save states: video restoration and UI save/load were tested; bit-exact audio
restoration fails in the legacy serializer. Other cores have API/load tests,
not equivalent gameplay coverage. Game buttons do not trigger fast-forward
or quit; F12 uses RetroArch's two-press quit confirmation.

## Cores directory

All libretro cores install to `/usr/lib/libretro/`:

```
/usr/lib/libretro/
├── fceumm_libretro.so              # NES/Famicom/FDS
├── gambatte_libretro.so            # Game Boy / Game Boy Color
├── mgba_libretro.so                # Game Boy Advance
├── snes9x_2005_libretro.so         # SNES
├── genesis_plus_gx_libretro.so     # SMS/GG/MD
├── mednafen_pce_fast_libretro.so   # PC Engine
├── fbneo_libretro.so               # Arcade
└── handy_libretro.so               # Atari Lynx
```

## PCSX4ALL (PS1)

PS1 is **not** handled by a libretro core. Instead, we ship the
[PCSX4ALL "rkgame reconstruction"](https://github.com/goph-R/SF3000-RE)
which was extracted from the stock SF3000 firmware. This remains an
experimental integration: the LilSmokeOS physical BSP and ABI must be
validated before claiming it works on the target console.

Why not a libretro PS1 core?
1. **Performance.** PCSX4ALL uses a MIPS-to-MIPS dynamic recompiler
   (dynarec) that runs PS1 code natively on the 74Kf CPU. This is
   potentially faster than an interpreter. No matched LilSmokeOS physical
   benchmark has been collected in this performance investigation.
2. **Compatibility.** The rkgame reconstruction was tested against the
   real SF3000/GB350 hardware during the firmware reverse-engineering
   project. That does not establish compatibility or full speed for this
   different Linux/musl integration; games and board drivers need testing.
3. **Memory.** The dynarec uses the lower 2 GB of virtual address space
   for fixed PS1 memory mappings. A libretro core would have to fight
   the host process for that space.

### Architecture

```
┌──────────────────────────────────────────────────────┐
│  pcsx4all-launch  (~12 KB, the binary LilSmokeUI     │
│                    execve()s)                         │
│       │                                               │
│       │  calls via rkgame_bridge.h:                   │
│       │   PCSX_Load(game_path)                        │
│       │   pcsx_Thread(NULL)  ← blocks until user quit │
│       ▼                                               │
│  libpcsx4all_rkgame.a  (4 MB pre-built static lib)   │
│   • R3000A MIPS dynarec (mips_to_mips)               │
│   • GPU: gpu_unai via gpulib                         │
│   • SPU: spu_pcsxrearmed                             │
│   • CDROM, MDEC, GTE, pad, SIO                       │
│   • rkgame port (src/port/rkgame/)                   │
│       │                                               │
│       ▼                                               │
│  SDL 1.2 (already on device as part of stock firmware│
│  libs — we link against it for compatibility)        │
└──────────────────────────────────────────────────────┘
```

### Build

The static library `libpcsx4all_rkgame.a` is **pre-built** and shipped
in the package source tree (`package/pcsx4all/src/`). We do NOT rebuild
it because:
1. Its provenance is the stock reconstruction, not a verified LilSmokeOS deployment
2. It uses a MIPS dynarec with specific toolchain requirements
3. Saves build time (~3 min vs ~30 sec to just link)

The launcher (`pcsx4all-launch.c`) is a tiny (~150 line) C program that:
1. Allocates a 640×480×16 video buffer
2. Sets up callbacks via `rkgame_pcsx_set_*` API
3. Calls `PCSX_Load(game_path)` to load the disc
4. Spawns `pcsx_Thread(NULL)` which runs the emulator until the user quits

### BIOS

PS1 BIOS is required. Users must provide `scph1001.bin` (North America,
512 KB) and place it at `/persist/bios/scph1001.bin`.

We cannot ship the BIOS due to copyright. The launcher prints a clear
warning if the BIOS is missing.

### Saves & states

| Path | Purpose |
|---|---|
| `/persist/saves/pcsx4all/<gamename>.mcr` | Memory card saves |
| `/persist/states/pcsx4all/<gamename>.state` | Save states |

These are created automatically on first run.

### ROM formats

PCSX4ALL supports:
- `.bin` + `.cue` (most common)
- `.img` + `.cue`
- `.pbp` (PlayStation Portable format — works for PS1)
- `.ecm` (with `.cue`)

The `lilsmoke-launch psx <rom>` wrapper auto-detects a sibling `.cue`
file for `.bin`/`.img` inputs.

LilSmokeOS also accepts `.7z` ROM archives for non-arcade systems. The
launcher extracts them to `/storage/.cache/lilsmoke/archives/<system>/`
and reuses the extracted file until the archive changes. Arcade
`.zip`/`.7z` romsets are passed through directly to FBNeo.

## Launching games

LilSmokeUI (J3) will call `/usr/lib/lilsmoke/lilsmoke-launch <system> <rom>`:

```sh
# NES
lilsmoke-launch nes /storage/roms/nes/SuperMarioBros.nes

# SNES
lilsmoke-launch snes /storage/roms/snes/ChronoTrigger.sfc
lilsmoke-launch snes /storage/roms/snes/ChronoTrigger.7z

# PS1
lilsmoke-launch psx /storage/roms/psx/CrashBandicoot.bin

# Arcade
lilsmoke-launch arcade /storage/roms/arcade/sf2.zip
lilsmoke-launch arcade /storage/roms/arcade/mslug.7z
```

The launch script auto-routes PS1 to `pcsx4all-launch` and everything
else to `retroarch -L <core>`.

## Performance tuning

For 128 MB RAM SKUs (LILSMOKE_LITE preset):
- Drop FBNeo, Handy, Beetle PCE Fast cores
- Reduce `ASSET_CACHE_MB` from 16 to 8
- Disable Dropbear by default
- Validate PCSX4ALL's working set with the actual game and BSP; do not assume it fits

For very heavy PS1 games (e.g. Gran Turismo):
- Enable frame skip in `/etc/pcsx4all.cfg`: `frame_skip = 1`
- Disable rewind if enabled
- Close other background processes

## What's NOT in J2 (deferred)

- **Online core updates** — would require RetroArch's network updater, which we explicitly disable (we ship a curated set)
- **Netplay** — would require upstream network code, not useful on a handheld
- **Achievements** — RetroArch cheevos require SSL + more memory, disabled
- **PS1 PGXP** (perspective-correct textures) — not in the rkgame reconstruction
- **N64, PSP** — out of reach for 1.2 GHz MIPS + 256 MB RAM

## Adding a new core (future)

To add a new libretro core:

1. Add a six-field entry to `package/retroarch-mips/cores.lock` with an exact
   40-character source commit, build file and built/installed filenames.
   Add the core name to `LILSMOKE_CORES` and vendor its info file with provenance.
2. Add the backend routing case to `lilsmoke-retroarch`, then test target ELF,
   API loading, ROM execution, inputs, saves and working-set memory.
3. Rebuild: `./scripts/build.sh qemu-mipsel`

The build script clones the repo, builds the core with the right MIPS
CFLAGS, and installs the `.so` to `/usr/lib/libretro/`.
