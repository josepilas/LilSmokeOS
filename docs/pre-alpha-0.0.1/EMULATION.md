<!-- SPDX-License-Identifier: MPL-2.0 -->
# Games And Emulator Selection

Use games and BIOS files you are entitled to use. This release must not bundle
commercial ROMs, BIOS, WADs, game executables or extracted SM64 assets.

## Add Content

Copy files into `LILROMS/roms/<system>/` on the PC, then safely eject and use
**Settings > Scan games now**. These are common canonical folder IDs:

| System | Folder ID | Typical content |
| --- | --- | --- |
| NES | `nes` | `.nes`, `.unif`, `.fds` |
| Super Nintendo | `snes` | `.sfc`, `.smc` |
| Game Boy / Color / Advance | `gb`, `gbc`, `gba` | `.gb`, `.gbc`, `.gba` |
| Mega Drive / Master System / Game Gear | `md`, `sms`, `gg` | `.md`, `.gen`, `.sms`, `.gg` |
| PC Engine | `pce` | `.pce` |
| PlayStation | `psx` | `.cue`+tracks, `.pbp`, supported disc images/playlists |
| Nintendo 64 | `n64` | `.z64`, `.v64`, `.n64` |
| Arcade | `arcade` | Matching emulator ROM sets |
| Atari | `atari2600`, `atari7800`, `atari800` | System-specific ROM/disk formats |
| Lynx / Neo Geo Pocket / WonderSwan / Pokemon Mini | `lynx`, `ngpc`, `wswan`, `pokemini` | Provider-specific formats |

These IDs are catalog definitions, **not promises that every provider or game
is installed, compatible or fast**. Settings filters choices by installed
providers. BIOS normally belongs in `/storage/bios`; required names/versions
are engine-specific. Avoid guessing BIOS identity from its extension.

Preserve complete CD sets: a CUE needs its referenced tracks; M3U disc lists
need their referenced files. Keep them together with unchanged relative names.
Do not launch an arbitrary PSX BIN as if it were a standalone cartridge ROM.

## Compressed Games

The shared resolver supports supported ZIP/7z and other archive inputs before
handoff, rather than requiring each emulator to implement the same decoder.
It rejects unsafe paths and bounds extraction. Archives need space for an
extracted cache; very large compression dictionaries can exceed memory limits.
Prefer one game per cartridge archive and include all disc/project companions.

Compatibility is engine-specific. Arcade cores require matching ROM-set versions
and may need ZIP containers, not a single extracted file. Unsupported content,
ambiguous archives, encrypted archives or insufficient cache space produce an
error. If a game fails from an archive, try the supported uncompressed file/set
without deleting the original. Explorer extraction and ROM routing are distinct.

## Choose A Provider

In **Settings > Emulator Core Settings**, left/right chooses global RetroArch
or OpenDingux policy. Changing global policy clears per-system exceptions.
Press A on the setting to choose a particular system and an installed provider,
or **Follow global setting**. An unavailable explicit choice fails visibly;
it is not silently replaced by another emulator.

Native entries from the **OpenDingux** home view open their own application
menus. Selecting a ROM from a console list launches that console's selected
provider directly. Legacy OpenDingux OPKs may require source adaptation/rebuild
for musl, native display/audio and the M8 kernel; the shared name does not
establish binary compatibility.

PCSX4ALL now lives in **LilSmoke Applications** and declares `open_core=psx`.
Choose its LilSmoke App entry for PSX. RetroArch PCSX ReARMed is a separate
option; owner feedback favors PCSX4ALL on this hardware. Native ReGBA is a
qualified GBA alternative to the slower native mGBA path. Game-specific accuracy
and speed can differ; do not delete a working provider to try another.

## Classic PC Engines

Optional registered content includes `doom` (PrBoom/IWAD), `cavestory`
(NXEngine/Doukutsu.exe plus data), `easyrpg` (RPG_RT.ldb project), `scummvm`
(`.scummvm`/`.svm` marker plus game data), and `openbor` (`.pak`). Supply the
engine-compatible full project/data set. A data marker EXE is not run as a
Windows application. Engines/paired providers appear only when qualified and
installed; a name in source definitions is not a shipping inventory.

## Performance Expectations

The CPU-only M8 cannot guarantee all PSX/N64 games at full speed. Start with the
tested provider/defaults, disable expensive effects, and measure on the device.
Host or QEMU FPS is not M8 FPS. Overclocking is not a substitute for a verified
board clock/thermal driver. See [SM64 release limits](APPLICATIONS.md) and
[troubleshooting](troubleshooting.md).
