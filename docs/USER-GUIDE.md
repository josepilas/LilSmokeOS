# LilSmokeOS — User Guide

This is the end-user guide for LilSmokeOS — what to do once you've got
the device (or the QEMU/VirtualBox VM) booted. If you're looking for
build instructions, read [build.md](build.md) and
[vbox-guide.md](vbox-guide.md) first.

## 1. What is LilSmokeOS?

LilSmokeOS is a from-scratch retro-handheld operating system for the
SF3000 and GB350 family of MIPS-based handhelds (and QEMU / VirtualBox
for development). It boots in <8 seconds, idles at 14 MB of RAM, runs
PS1 at full speed via a MIPS-to-MIPS dynamic recompiler, and lets you
play NES, GB, GBC, GBA, SNES, Genesis, Master System, Game Gear, PC
Engine, arcade (CPS1/early CPS2), Atari Lynx, and PS1 games.

Things it deliberately does *not* do:

- No N64, PSP, Dreamcast, PS2 (the hardware can't).
- No OpenGL/GLES (no GPU).
- No desktop environment (no X, no Wayland, no systemd).
- No browser, no YouTube, no Netflix.
- No app store (we ship a curated set).

## 2. Getting it onto your device

### Real hardware (SF3000 / GB350)

1. Follow [hardware.md](hardware.md) — the short version is: download
   the latest `.img` from the releases page, flash it to a MicroSD card
   with `dd` or BalenaEtcher, insert into the device, power on.
2. The first boot takes ~15 seconds (longer than normal) because the
   `/persist` partition is being initialised.
3. You'll see the LilSmokeOS logo, then the system list.

### VirtualBox (fastest dev preview)

1. Read [vbox-guide.md](vbox-guide.md).
2. Run `scripts/run-vbox.sh` — it auto-creates a VM with the right
   settings (256 MB RAM, 640×480, no GPU acceleration) and boots the
   ISO.

### QEMU (MIPS development)

1. Build the MIPS target: `./scripts/build.sh qemu-mipsel`.
2. Run: `./scripts/run-qemu.sh`.

## 3. The main menu

When LilSmokeOS boots, you land on the **System List** — a vertical
list of every console LilSmokeOS supports, in alphabetical order:

```
┌─────────────────────────────────────────────┐
│                LilSmokeOS                    │
│                                              │
│   > Arcade                                   │
│     Atari Lynx                               │
│     Game Boy                                 │
│     Game Boy Color                           │
│     Game Boy Advance                         │
│     Genesis / Mega Drive                     │
│     Master System                            │
│     ...                                      │
│                                              │
│   Settings                          (Start)  │
└─────────────────────────────────────────────┘
```

Navigation:

- **D-pad up/down**: move selection
- **A** (or **Start** on a system): enter the system's game grid
- **Start** (at the bottom of the list): open Settings
- **B**: nothing at the top level (or quit if you're in a submenu)

## 4. LilSmoke Explorer

The system list includes **LilSmoke Explorer**, a lightweight file
manager for browsing `/storage`, `/persist`, and mounted media. It can
copy, move, rename, delete, create files/folders, view images and text,
edit small text files, and extract `.zip`, `.rar`, `.tar.gz`, `.7z`, and
other common archives.

See [lilsmoke-explorer.md](lilsmoke-explorer.md) for controls and limits.

## 5. Adding ROMs

ROMs live in `/storage/roms/<system>/`. The `/storage` partition is
FAT32 — you can pull the SD card and drop ROMs on it from any OS,
exactly like the stock firmware.

### Folder layout

```
/storage/roms/
├── arcade/         ← .zip / .7z files (FBNeo format)
├── atarilynx/      ← .lyx / .lnx
├── gb/             ← .gb
├── gbc/            ← .gbc
├── gba/            ← .gba
├── genesis/        ← .bin / .md / .smd
├── mastersystem/   ← .sms
├── gamegear/       ← .gg
├── nes/            ← .nes / .fds
├── pcengine/       ← .pce
├── psx/            ← .bin + .cue (or .pbp / .ecm + .cue)
└── snes/           ← .sfc / .smc
```

LilSmokeOS scans these on startup. If you added ROMs while the device
was running, go to **Settings → Scan ROMs now** (or just reboot).

Non-arcade systems also accept `.7z` archives. LilSmokeOS extracts them
to `/storage/.cache/lilsmoke/archives/` on first launch and reuses the
cached file until the archive changes.

### ROM naming

- Filenames should be ASCII. Unicode is supported but the default font
  only covers Latin-1.
- Long filenames are truncated with `…` in the grid view.
- For PS1, the `.bin` and `.cue` must have the same basename
  (`CrashBandicoot.bin` + `CrashBandicoot.cue`). Select the `.bin` in
  the UI; LilSmokeOS auto-loads the `.cue`.

### BIOS files

Some systems require BIOS files. Put them in `/persist/bios/`:

| System | File | Required? |
|---|---|---|
| PS1 (NTSC) | `scph1001.bin` | Yes — without it PCSX4ALL refuses to boot |
| PS1 (PAL) | `scph1002.bin` | Optional (use scph1001 for NTSC games) |
| PC Engine CD | `syscard3.pce` | Only for CD games |

We cannot ship BIOS files due to copyright. The LilSmokeUI grid view
shows a small ⚠ next to systems that need a BIOS but don't have one.

## 6. Playing a game

1. Pick a system → press A.
2. Pick a game → press A.

LilSmokeUI `execve()`s the emulator (RetroArch or PCSX4ALL), which
replaces the frontend process entirely. The kernel reclaims the
frontend's memory while the game runs.

In-game:

- **A / B / X / Y / L / R**: as expected for the system
- **Start + Select together**: open the in-game menu (RGUI for
  RetroArch, the PCSX4ALL menu for PS1)
- **Select + L + R**: save state (RetroArch cores only)
- **Power** (long-press on real HW): quit the emulator and return to
  LilSmokeUI

When you exit the emulator, LilSmokeUI respawns (via inittab) and
you're back at the system list with your last selection highlighted.

### Save files

Battery saves (`.srm`) and save states (`.state`) live on `/persist`:

```
/persist/saves/<system>/<romname>.srm
/persist/states/<system>/<romname>.state
```

For PS1:

```
/persist/saves/pcsx4all/<romname>.mcr
/persist/states/pcsx4all/<romname>.state
```

These survive reboots and OTA updates.

## 7. Settings

Open with **Start** at the bottom of the system list. Settings are
written to `/persist/config/lilsmoke.conf` immediately.

| Setting | Values | Notes |
|---|---|---|
| WiFi | On / Off | Off by default. See [wifi-ota.md](wifi-ota.md). |
| SSH (Dropbear) | On / Off | Requires WiFi on. |
| Display rotation | 0 / 90 / 180 / 270 | Useful on portrait panels. |
| Scaling mode | integer / stretch / aspect | Integer = pixel-perfect. |
| Master volume | 0–100 | Persisted across reboots. |
| CPU governor | ondemand / performance / powersave | performance = max battery drain. |
| Scan ROMs on boot | On / Off | Off = faster boot. |
| Auto-sleep | Off / 1 / 5 / 10 / 30 min | Screen off after N minutes idle. |
| LCD brightness | 0–100 | Real HW only. |
| Input — Button Mapping | (submenu) | See [input.md](input.md). |
| OTA Update | (submenu) | See [ota.md](ota.md). |
| Theme | (list) | See [THEME-GUIDE.md](THEME-GUIDE.md). |

## 8. Themes

LilSmokeOS ships a default theme using the project palette (dark grey
+ dark green + dark blue + light beige). You can install additional
themes — including EmuELEC and ArkOS themes — by dropping them into
`/storage/themes/<name>/`.

See [THEME-GUIDE.md](THEME-GUIDE.md) for the format.

## 9. WiFi and online features

WiFi is off by default. When you turn it on (Settings → WiFi), you'll
need to enter your SSID and password once. LilSmokeOS writes a
`wpa_supplicant.conf` to `/persist/config/wifi.conf`.

With WiFi on, you get:

- **SSH access** — enable it in Settings. Default user is `root` with
  no password (set one if you care).
- **OTA updates** — Settings → OTA Update → Check.
- **Scraping** — auto-runs the next time you open a system that has
  unscraped ROMs. Pulls box art from Screenscraper.fr (you need a free
  API key — see [wifi-ota.md](wifi-ota.md)).

## 10. Updating (OTA)

1. Settings → OTA Update → Check.
2. If an update is available, → Download. The bundle (~25 MB) is
   downloaded to `/persist/updates/`.
3. → Install. The new rootfs is written to the inactive A/B slot.
4. → Activate. The next boot will use the new slot.
5. → Reboot.
6. If the new slot fails to boot 3 times in a row, the bootloader
   automatically rolls back to the previous slot. You don't need to do
   anything.

See [ota.md](ota.md) for the full A/B partition spec.

## 11. Power management

- **Power button (short press)**: nothing (you're in a game → quit
  back to UI; you're in UI → nothing).
- **Power button (long press, ~2 s)**: quit current app or, if in UI,
  open the power menu (Sleep / Reboot / Power off).
- **Auto-sleep**: if enabled in Settings, the screen turns off after N
  minutes of no input. Press any button to wake. The current game is
  NOT paused — you'll need to use the emulator's pause feature.
- **Battery**: real HW has a battery readout in the top-right of the
  UI. LilSmokeOS does NOT support charging-while-off — power must be
  on to charge.

## 12. Troubleshooting

### "The UI is just a black screen"

Most likely DirectFB can't find `/dev/fb0`. Check:

- Real HW: is the panel connected? Try the UART debug serial
  ([hardware.md](hardware.md)).
- QEMU: did you pass `-display sdl`? See `scripts/run-qemu.sh`.
- VirtualBox: see [vbox-guide.md](vbox-guide.md) — VBox requires VESA
  fbdev mode.

### "My ROM doesn't show up"

- Is the file extension one LilSmokeOS recognises? (See §4.)
- Is the file in the right system folder? `gb` ROMs in `gbc/` won't
  show up.
- Did you scan? Settings → Scan ROMs now.

### "PS1 game crashes immediately"

- Do you have `scph1001.bin` in `/persist/bios/`?
- Is the `.cue` file alongside the `.bin`? Open the `.cue` in a text
  editor — the `FILE "..." BINARY` line must match the `.bin`'s actual
  filename.

### "Audio is crackly"

- Try Settings → CPU governor → performance.
- For PS1 specifically, edit `/etc/pcsx4all.cfg` and set
  `frame_skip = 1`.
- For RetroArch cores, edit `/etc/retroarch.cfg` and lower
  `audio_sample_rate` to 32000.

### "I enabled WiFi and now the UI is slow"

`wpa_supplicant` + `dropbear` together use ~5 MB RSS. On 128 MB SKUs
that's noticeable. Disable them when not in use (Settings → WiFi off).

### "My save is gone"

Saves are in `/persist/saves/<system>/`. If they're not there:

- Did you flash a fresh image? `/persist` is preserved across OTA but
  NOT across a full reflash of the SD card.
- Did you reformat `/storage`? `/storage` is also where saves live on
  some legacy builds — check both.

## 13. Getting help

- Open an issue on the project's issue tracker.
- Include the contents of `/persist/config/lilsmoke.conf` and the
  output of `lilsmoke-update status`.
- If you have a real device, include a UART log
  ([hardware.md](hardware.md) explains how).
