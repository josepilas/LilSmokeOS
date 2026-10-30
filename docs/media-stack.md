# LilSmokeOS — Media Stack (J1)

This document describes the J1 media stack: DirectFB, SDL2, ALSA, libevdev,
fonts, and the fbdev test app that proves the stack works end-to-end.

## Architecture

```
┌──────────────────────────────────────────────────┐
│  LilSmokeUI / RetroArch / pcsx4all-launch        │  (J3, J2)
│        uses SDL2 API                             │
├──────────────────────────────────────────────────┤
│  SDL2 (DirectFB backend, no OpenGL/GLES)         │
│   • SDL_VIDEODRIVER=directfb                     │
│   • SDL_AUDIODRIVER=alsa                         │
├──────────────────────────────────────────────────┤
│  DirectFB 1.7.7 (fbdev-only, no GPU)             │
│   • /etc/directfbrc forces fbdev                 │
│   • No X11, no multi-app core                    │
├──────────────────────────────────────────────────┤
│  Linux fbdev driver (/dev/fb0)                   │
│   • SF3000 854x480 / GB350 640x480 @ 32bpp BGRA  │
└──────────────────────────────────────────────────┘

  Input:
┌──────────────────────────────────────────────────┐
│  evdev (/dev/input/event*)                       │
├──────────────────────────────────────────────────┤
│  libevdev (thin userland API over evdev)         │
└──────────────────────────────────────────────────┘

  Audio:
┌──────────────────────────────────────────────────┐
│  ALSA-lib (no PulseAudio, no dmix daemon)        │
├──────────────────────────────────────────────────┤
│  Kernel ALSA driver                               │
└──────────────────────────────────────────────────┘
```

## What J1 ships

| Package | Purpose | RSS impact |
|---|---|---|
| `directfb-mips` (meta) | Pulls in Buildroot's DirectFB with fbdev-only config | 4-6 MB |
| `sdl2` (DirectFB backend) | 2D rendering + input + audio API for LilSmokeUI/RetroArch | 2-3 MB |
| `alsa-lib` + `alsa-utils` | Sound card access + mixer tools | 1 MB |
| `libevdev` | Gamepad input parsing | <100 KB |
| `libpng` + `libjpeg-turbo` + `libwebp` | Cover art + theme asset loading | ~700 KB |
| `p7zip` (`7za`) + `unrar` | Storage-backed `.7z`/ZIP/TAR/RAR extraction | ~900 KB |
| `freetype` + `fontconfig` + `dejavu` | TTF font rendering for the UI | 1 MB |
| `lilsmoke-fbtest` | J1 smoke test — paints the logo to /dev/fb0 | 12 KB |

## DirectFB configuration

`/etc/directfbrc` (installed by post-build.sh):

```ini
no-hardware        # No GPU on SF3000/GB350
no-sighandler
quiet
vt
no-vt-switch
no-vt-switching
fbdev=/dev/fb0
mode=854x480        # SF3000; GB350 uses 640x480
depth=32
pixelformat=ARGB
backbuffer
vsync-after
no-input           # SDL2 handles input
surface-cache-size=8388608
mmx=off
sse=off
```

## Environment variables (set in /etc/profile.d/lilsmoke.sh)

```sh
export SDL_VIDEODRIVER="directfb"
export SDL_AUDIODRIVER="alsa"
export DFBARGS="no-hardware,no-sighandler,quiet"
```

These ensure any SDL2 application picks DirectFB instead of trying to
contact an X server (which doesn't exist on this device).

## J1 smoke test: lilsmoke-fbtest

Installed at `/usr/bin/lilsmoke-fbtest`. Source: `package/lilsmoke-fbtest/src/lilsmoke-fbtest.c`.

What it does:
1. Opens `/dev/fb0` and queries geometry via `FBIOGET_VSCREENINFO`
2. mmaps the framebuffer
3. Loads `/usr/share/lilsmoke/logo.png` (the converted logo PNG)
4. Centers it on the framebuffer using direct pixel writes
5. Falls back to a procedurally-drawn "L" if PNG is missing
6. Prints diagnostics to stderr (geometry, pixel format, alpha offsets)
7. Holds for 10 seconds, then exits

Run it manually:
```sh
lilsmoke-fbtest /usr/share/lilsmoke/logo.png
```

Or automatically on boot: `S50fbtest` runs it after ALSA setup (only if `/dev/fb0` exists — skipped in QEMU if no fbdev display is configured).

## Verifying J1 in QEMU

QEMU's malta board emulates the vt8623 VGA framebuffer. To test:

```sh
./scripts/run-qemu.sh
```

Watch the boot log for:
```
[lilsmoke] S40: ALSA restore
[lilsmoke] S50: running fbdev smoke test (10s)...
[lilsmoke-fbtest] J1 framebuffer smoke test
[lilsmoke-fbtest] screen: 640x480 @ 32bpp
[lilsmoke-fbtest] mmap'd 1228800 bytes at 0x...
[lilsmoke-fbtest] cleared screen
[lilsmoke-fbtest] logo loaded: 999x414
[lilsmoke-fbtest] logo blitted at (-179, 33)
[lilsmoke-fbtest] displaying for 10 seconds...
```

The QEMU SDL window should show the LilSmokeOS logo centered on a dark grey background for 10 seconds, then drop to the BusyBox shell.

If you see `screen: 640x480 @ 32bpp` but no logo appears, the framebuffer is working but the logo file is missing. Check `/usr/share/lilsmoke/logo.png` exists.

If you see `cannot open /dev/fb0`, you're running QEMU without a display. Add `-display sdl` to the QEMU command line (the run-qemu.sh script already does this).

## Performance budget (per-frame, 60 FPS = 16.67 ms)

| Operation | Cost | Notes |
|---|---|---|
| DirectFB flip | ~0.5 ms | Single blit on 640x480x32 |
| SDL2 event poll | <0.1 ms | evdev is very fast |
| RetroArch video frame | 4-8 ms | Depends on core (NES faster than SNES) |
| ALSA audio chunk | ~0.5 ms | 1024-sample buffer at 44.1 kHz |
| Total headroom | ~7-11 ms | Comfortable for 60 FPS on most cores |

PS1 (PCSX4ALL) is the exception — its MIPS dynarec + software renderer can
use the full 16 ms on demanding games. Frame skip is available via
`pcsx4all.cfg`.

## What's NOT in J1 (deferred)

- **Hardware-accelerated blits** — no GPU means everything is CPU
- **OpenGL/GLES** — not available
- **Hardware video decode** — not available (not needed; all our systems are software-rendered)
- **Wayland** — explicitly disabled (we use raw fbdev via DirectFB)
- **PulseAudio** — explicitly disabled (ALSA direct is lighter)

These constraints are baked into the device hardware. They cannot be
lifted by software.
