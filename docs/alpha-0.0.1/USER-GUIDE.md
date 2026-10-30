<!-- SPDX-License-Identifier: MPL-2.0 -->
# Alpha-0.0.1 User Guide

For the SEGAM-M8-V6.0 release. See [installation and limitations](README.md)
and [changes since Pre-Alpha](RELEASE-NOTES.md). Older J-job examples and the
Pre-Alpha guides are not the current hardware/feature specification.

## Storage And Games

The PC-visible partitions are **LILOS** (system/boot) and **LILROMS** (content).
Do not delete unknown boot files or format LILOS to fix a game launch problem.

Place games under `LILROMS/roms/<system>/`, for example `snes`, `gba`,
`md` or `psx`. Put required BIOS/game data in the provider's documented
location. No ROM or BIOS collection is included in the release.

Use **Settings -> Scan games now** after adding or removing games. **Scan ROMs
on boot** controls startup discovery. Returning from a game can reuse the
unchanged cached library; it should not trigger a full scan every time.

ZIP/7z and other supported archives pass through a shared resolver where
appropriate. Not every archive is a single ROM: arcade sets, disc cue sheets,
track files and multi-file data projects have engine-specific requirements.
See [emulator/content rules](../cores.md).

## Navigate And Find Games

| Control | Action |
| --- | --- |
| D-pad / mapped navigation input | Move selection |
| A / confirm | Open the selected system, game or item |
| B / back | Return or cancel |
| Y in a game list | Search with the on-screen keyboard |
| Hold R2 in a game list | Move down quickly |
| X in a game list | Open Game Options, including Favorites |
| Start+X in integrated emulators | Open the emulator menu |
| Start+Select in integrated emulators | Exit/return to LilSmokeUI |

**Recently Played** and **Favorites** are separate home collections. **Start in
Recents** is available in Settings. Collections reference the original content;
removing a favorite does not delete its ROM.

Controller labels depend on the selected mapping. A third-party application
must implement its input/quit contract; arbitrary software does not acquire
every shortcut automatically.

## Emulator Core Settings

In Settings, highlight **Emulator Core Settings**:

- Left/right selects the global RetroArch/OpenDingux policy.
- Confirm opens per-system choices, including installed compatible LilSmoke
  Applications that declare `open_core`.
- A per-system choice overrides the global policy. Changing global policy
  clears existing per-system exceptions.
- Missing explicit providers produce a launch error rather than silently
  switching to a different engine.

Opening an emulator in **OpenDingux** opens its own application/menu.
Opening a ROM from a console list passes it to that system's selected provider.
Those are different workflows.

PCSX4ALL is an independent Application and the preferred PS1 provider for this
M8 lineage. In compatible engines use **Start+X -> Save State / Load State**.
Choose the intended slot and verify a successful save before relying on it.
Memory-card/SRAM saves and emulator save states are different. Not every core
can serialize its state, and states are not interchangeable between engines.

## Themes And TV Display

Copy a complete theme set to `LILROMS/themes/<name>/`, preserving its XML,
subfolders, fonts and images. Select it under **Settings -> Theme**.

**Theme system layout**, **Theme game list** and **Theme transition** expose
supported variants. Transitions include auto, slide, fade and instant. The
theme engine now renders motion; it is not limited to the early static subset.

For TV cropping or sizing use **Presentation**, **UI rendering**, **TV safe
margin (%)** and **Scaling**. A 16:9 menu does not require stretching a 4:3
game. Large themes may still be CPU-bound; try balanced rendering or a lighter
theme rather than assuming a GPU is available.

See [theme compatibility](../theme-compat.md) for supported widgets, fixed
background behavior and deliberate gaps. Applications retain their independent
design instead of inheriting an EmulationStation theme.

## LilSmoke Applications

Open a trusted `.lilpck` in **LilSmoke Explorer**. Review the icon/name/version
in the installer, then choose **INSTALL** or **CANCEL**. Progress includes a
percentage and processed MB.

Applications are installed in `LILROMS/independent-apps/<name>/`. Press **Y**
in Applications for Rename or Uninstall. Uninstalling deletes data inside that
app folder. Deleting its folder manually also removes the app from the grid.

An update can move to a newer or older package version after confirmation.
It replaces package-owned files, removes obsolete owned files and preserves
user-created data subject to path conflicts. Same-version packages report
**Already installed.** Keep personal saves backed up.

A package has `meta.json` and exactly one root `.sh`; the script need not be
called `run.sh`. Subfolders can contain additional scripts. Packages bundle
their runtimes but still need a matching CPU/libc/device contract.
See the [Applications guide](../lilsmoke-applications.md).

## TreeFrog Ports

Native compatibility is **separate** from Applications:

1. Copy the original port's companion `.sh` and game directory together to
   `LILROMS/treefrog-ports/`.
2. Open **TreeFrog Ports** on the home screen and select the port.
3. Hold **Start+Select** on one controller for roughly half a second to exit.

Keep the original structure and supply required legal game data. Matching
device libraries/legacy PicoArch may still be needed by the original port.

If direct compatibility cannot handle a port, try the optional
[Port Converter](https://josepilas.github.io/LilSmoke-Port-Converter/).
It analyzes/converts supported ZIP contents locally in the browser and returns
a `.lilpck` for Applications. It cannot make an ARM/Windows/GPU-only binary
run on this MIPS board. See [direct compatibility vs conversion](../treefrog-port-converter.md).

## Music, Videos, Photos And Shell

Open supported media files in **LilSmoke Explorer** to launch the media player.
Playback controls cover pause/resume, seeking, track selection and the relevant
playlist/photo operations; availability depends on media and decoder support.
Use lower-resolution videos on the CPU fallback path. Smooth HD playback and
every format are not guaranteed.

**Settings -> Shell Terminal** opens the Linux shell terminal for a connected
keyboard. Commands execute on the real device with access to its files:
avoid unknown destructive commands. FFmpeg/ffprobe are included for supported
media tasks, not an instruction to run expensive background transcodes.

## Controllers And Hardware-Dependent Features

Use **Settings -> Gamepad mapping** to capture bindings. RK707 D-Input,
PS-Input and X-Input paths and RetroArch analog mapping received fixes;
reconnect/mode-change recovery does not guarantee every receiver works.

Mouse/touchpad use, pointer speed and cursor size have settings. Wi-Fi, SSH,
Bluetooth, USB network/LILDRIVE, ZRAM and overclocking depend on actual driver/
kernel support. A visible setting alone is not evidence that the device has
that capability. LILDRIVE must never export LILOS.

## Safe Power Off

1. Exit the game/application and open **Settings -> Power Off**.
2. Confirm the request.
3. On success, wait through **Power off operation succeeded** for ten seconds
   and the subsequent black screen before unplugging power.
4. A warning such as storage needing repair is not success. A black TV alone
   is not proof of a clean shutdown.

Do not pull power while installing, expanding a partition or writing saves.
Safe shutdown cannot repair a failing card or guarantee against sudden power
loss. Back up content and saves regularly.

## Report A Problem

Include the device/stock-firmware revision, OS version, provider/core, theme,
controller mode and exact error. Preserve relevant boot/application logs and
note whether the issue happens with the default theme too. Do not post ROMs,
BIOS dumps, private game data, credentials or a whole personal card image.

Report issues at [josepilas/LilSmokeOS](https://github.com/josepilas/LilSmokeOS/issues).
