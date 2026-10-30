# LilSmokeOS — Architecture

## 1. Hardware reality

| Constraint | Value | Implication |
|---|---|---|
| ISA | MIPS32r2, 74Kc-class + DSP ASE + FPU | ARM/NEON emulator tricks are unavailable; use pure C or MIPS dynarec cores |
| Cores | 2 × 1.2 GHz | One for emulator, one for system — no headroom for bloat |
| RAM | 128–256 MB DDR3 | Hard ceiling. No desktop stack, no X, no systemd, no glibc, no PulseAudio |
| GPU | **None** | Framebuffer only. SDL2 over DirectFB. Every blit is CPU |
| Display | SF3000 854×480; GB350 640×480 | UI and themes must use runtime dimensions |
| Storage | FAT32 SD up to 256 GB | Slow random I/O → squashfs + tmpfs critical |

## 2. Design principles (non-negotiable)

1. **Every megabyte is fought for.** On 128 MB SKUs: kernel ≤ 12 MB, userland ≤ 24 MB, leaving ~90 MB for emulator.
2. **No desktop assumptions.** No X, no Wayland, no systemd, no glibc, no PulseAudio. DirectFB + ALSA + musl + BusyBox.
3. **MIPS-first, not MIPS-ported.** Compile flags, dynarec backends, and memory layout chosen for 74Kf. Use DSP ASE in hot loops where it helps.
4. **Read-only root, writable overlay.** Powers-off safe; no fsck on boot.
5. **A/B partitions** for atomic updates with rollback (planned J4).
6. **LilSmokeUI must idle at <20 MB RSS.** EmulationStation-DE idles at 80–120 MB — we cannot afford that.
7. **No usable 3D stack means CPU discipline.** Pre-rendered native-size backgrounds, no live 3D compositing, and 320×240 cover art.

## 3. Layered architecture

```
┌──────────────────────────────────────────────────────────┐
│  USER LAYER                                               │
│  LilSmokeUI (C++17/SDL2/DirectFB, ~700KB binary)          │
│  • ROM list/grid views                                   │
│  • LilSmoke Explorer file manager + archive extraction   │
│  • EmuELEC + ArkOS theme loader (XML/png/webp, read-only)│
│  • Offline scraping + optional WiFi (Screenscraper FR)   │
│  • Settings, save management                             │
├──────────────────────────────────────────────────────────┤
│  EMULATION LAYER                                          │
│  RetroArch (libretro) — DIRECTFB video driver            │
│  Curated 8-core set:                                      │
│   • FCEUmm (NES)         • Genesis Plus GX (MD/SMS/GG)   │
│   • Snes9x 2005 (SNES)   • mGBA (GBA)                    │
│   • Gambatte (GB/C)      • FBNeo (CPS1/2 early arcade)   │
│   • Beetle PCE Fast      • Handy (Lynx)                  │
│  PCSX4ALL (rkgame reconstruction) — PS1 via MIPS dynarec │
│  (N64/PSP/DC explicitly NOT supported — hardware cannot) │
├──────────────────────────────────────────────────────────┤
│  MEDIA LAYER                                              │
│  DirectFB (2D blitter core)                              │
│  SDL2 (DirectFB backend, static-linked)                  │
│  ALSA-lib (no dmix daemon, exclusive HW access)          │
│  libevdev • libpng • libjpeg-turbo • libwebp • p7zip/unrar│
│  libvorbis + libtremor (low-RAM ogg decoder)             │
├──────────────────────────────────────────────────────────┤
│  CORE LAYER                                               │
│  C3100 board BSP boundary (kernel/DTB integration pending)│
│  musl libc (not glibc — saves ~2 MB RSS)                 │
│  BusyBox init • ubox • Dropbear SSH • iw + wpa_supp      │
│  OverlayFS • SquashFS • zram (48 MB compressed swap)     │
├──────────────────────────────────────────────────────────┤
│  BOOT LAYER                                               │
│  Verified board boot chain required before FIT flashing   │
│  (kernel + Ingenic dtb + initramfs in one FIT)           │
│  A/B rootfs selection with bootcounter rollback (J4)     │
├──────────────────────────────────────────────────────────┤
│  HARDWARE                                                 │
│  HCSEMI MIPS32r2 @ ~1.2 GHz • 128–256 MB DDR3            │
│  /dev/fb0 (DirectFB) • SD card (FAT32, up to 256 GB)     │
│  SF3000 854×480 / GB350 640×480 IPS displays             │
└──────────────────────────────────────────────────────────┘
```

## 4. Partition layout (SD card, planned for J4/J8)

| Part | Size | FS | Purpose |
|---|---|---|---|
| `p1` | 4 MB | raw | U-Boot + SPL + uEnv.txt |
| `p2` | 96 MB | squashfs | Rootfs A (read-only) |
| `p3` | 96 MB | squashfs | Rootfs B (read-only) |
| `p4` | 16 MB | ext4 | `/persist` (configs, saves, bootcounter) |
| `p5` | rest | FAT32 | `/storage/roms` + BIOS + saves + themes |

Bootloader picks A or B based on `/persist/bootcounter`. Failed boot → fallback.

> Note: stock SF3000 uses FAT32 across the whole card. LilSmokeOS will ship as a replacement image that re-partitions the card. User data on `/storage` is preserved across OTA.

## 5. Runtime memory budget (256 MB target)

| Component | RSS target |
|---|---|
| Kernel + slab | 12 MB |
| BusyBox init + udev-lite | 2 MB |
| DirectFB + SDL2 runtime | 6 MB |
| ALSA + input daemon | 1 MB |
| **LilSmokeUI** | **14 MB** |
| **Free for emulator** | **~190 MB** |
| zram swap | 48 MB (compressed) |

For 128 MB SKUs, ship "Lite" preset:
- Drop FBNeo, Handy, Beetle PCE
- Cover-art cache 8 MB max
- No Dropbear, no WiFi daemons by default
- Free for emulator: ~85 MB

## 6. LilSmokeUI — custom frontend

Why not EmulationStation-DE? It idles at 80–120 MB and assumes OpenGL. We have neither RAM nor GPU.

LilSmokeUI is from-scratch:

- **C++17**, single binary, ~700 KB stripped
- **Static-linked SDL2** (no .so overhead, no versioning issues)
- **Theme format:** native JSON + PNG/JPEG/WebP, **plus** read-only EmuELEC/ArkOS XML theme loader
- **Views:** grid + list; 4:3 native, no fancy transitions (saves CPU)
- **Asset cache:** LRU in RAM, max 16 MB (Lite: 8 MB)
- **Rom launch:** `execve()` RetroArch directly — frontend memory is reclaimed by kernel while game runs
- **Input:** evdev polling thread; built-in keymapper (no separate udev rules layer)
- **Settings:** flat key/value file in `/persist/config/lilsmoke.conf`
- **Network features off by default** — optional WiFi toggle enables scraping + OTA + Dropbear + web upload

### Theme compatibility

EmuELEC/ArkOS themes are XML with PNG/JPEG/WebP assets. We parse them read-only:
1. Parse `<theme>` XML on first load
2. Convert paths to absolute
3. Convert coordinates to our 640×480 space
4. Use image assets directly (no re-encoding)
5. Skip unsupported elements (videos, complex animations, shaders) gracefully

A user drops an EmuELEC theme folder into `/storage/themes/`, picks it in Settings → it works.

## 7. Build system

**Buildroot 2024.02 LTS** as the spine. Reasons:
- musl + uClibc-friendly
- Cross-compiles cleanly for MIPS
- Produces squashfs images out of the box
- Tiny output (~24 MB rootfs vs Yocto's 60 MB+)
- External tree pattern fits our needs perfectly

### Repo layout

```
lilsmoke-os/
├── README.md
├── ARCHITECTURE.md          ← this file
├── external.mk              ← Buildroot external hook
├── Config.in                ← top-level menuconfig
├── configs/                 ← defconfigs (qemu_mipsel, sf3000, gb350)
├── board/
│   ├── qemu-mipsel/         ← dev/QEMU target
│   │   ├── linux.config
│   │   ├── busybox.config
│   │   ├── post-build.sh
│   │   └── post-image.sh
│   └── sf3000/              ← real hardware (J4+)
│       ├── linux.config
│       ├── dtb/             ← Ingenic device trees from SF3000-RE
│       └── post-image.sh
├── package/
│   ├── lilsmoke-ui/         ← our frontend (J3)
│   ├── retroarch-mips/      ← RA + curated cores (J2)
│   ├── directfb-mips/       ← DirectFB build tweaks
│   └── lilsmoke-scripts/    ← boot, update, wifi helpers
├── scripts/
│   ├── build.sh             ← top-level wrapper
│   ├── run-qemu.sh          ← QEMU-mipsel launcher
│   └── mk-update-bundle.sh  ← OTA packer (J6)
├── assets/
│   └── logo/                ← LilSmokeOS logo + brand assets
└── docs/
    ├── build.md
    ├── porting.md
    ├── theme-spec.md
    └── performance.md
```

## 8. Realistic supported systems

| Console | Status on E3100 | Core |
|---|---|---|
| NES, Famicom, FDS | Excellent | FCEUmm |
| Game Boy, GBC | Excellent | Gambatte |
| GBA | Good | mGBA |
| SNES | Good | Snes9x 2005 (lighter than 2002/2010) |
| Genesis/Megadrive | Excellent | Genesis Plus GX |
| Master System, Game Gear | Excellent | Genesis Plus GX |
| PC Engine/TG16 | Good | Beetle PCE Fast |
| Arcade (CPS1, early CPS2, Neo Geo early) | Good | FBNeo |
| Atari 2600/7800 | Good | Stella |
| Lynx | Good | Handy |
| Neo Geo Pocket, WSC | Excellent | Lightweight cores |
| Odyssey2 | Good | O2EM |
| PS1 | **Excellent** | PCSX4ALL (rkgame reconstruction, MIPS dynarec) |
| N64, PSP, DC, PS2 | **Not supported** | Hardware cannot |

## 9. Job phases

| Job | Description | Status |
|---|---|---|
| **J0** | Buildroot skeleton: defconfig, toolchain (mipsel 74Kf), kernel bring-up | ✅ skeleton |
| **J1** | Media stack: SDL2/DirectFB, ALSA, libevdev, fonts | ⏳ |
| **J2** | RetroArch + 8 curated cores cross-compiled | ⏳ |
| **J3** | LilSmokeUI v0.1: list view, rom scan, launch RA | ⏳ |
| **J4** | Persistence: A/B partitions, overlayfs, save management | ⏳ |
| **J5** | Input system: hotplug, keymap, controller config UI | ⏳ |
| **J6** | WiFi + Dropbear + scraping + OTA | ⏳ |
| **J7** | Performance pass: -O3 LTO, zram, MIPS DSP ASE | ⏳ |
| **J8** | Image packaging: flashable `.img`, update bundle format | ⏳ |
| **J9** | Documentation: porting guide, theme spec, build-from-source | ⏳ |

## 10. Branding

- **Logo:** see `assets/logo/`
- **Palette (default theme):**
  - Dark grey: `#2B2D31`
  - Dark green: `#1F3D2B`
  - Dark blue: `#1E2A47`
  - Light beige: `#D9C9A3`
- **Themeable:** users can install EmuELEC or ArkOS themes; LilSmokeOS ships a default theme using the palette above.
