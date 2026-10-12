<!-- SPDX-License-Identifier: MPL-2.0 -->
# Pre-Alpha-0.0.1 Release Notes

**Draft, unreleased.** The requested public version name is
`Pre-Alpha-0.0.1`. Internal Candidate numbers and private application versions
are development identifiers, not earlier public OS releases.
The [project release index](https://github.com/josepilas/LilSmokeOS/releases)
does not imply a downloadable artifact for this draft.

## Intended Scope

- Lightweight TV-oriented LilSmokeUI, file management and configurable themes.
- Game scanning on boot and **Scan games now** in Settings.
- Global and per-system emulator selection, compressed-content resolution and
  native OpenDingux-derived source ports adapted to the M8.
- PCSX4ALL as a bundled LilSmoke Application and selectable PSX provider.
- `.lilpck` application installation, version changes, rename and uninstall;
  application-declared `open_core` emulator integration.
- Native controller handling, including the tested RK707 receiver modes and
  RetroArch analog support; keyboard, mouse and supported touchpad input.
- Music, video and photo opening from Explorer with bounded software decoding.
- A stock-folder Device Adapter on LILOS for Windows, Linux and macOS workflows.
- A guarded Power Off operation with a ten-second completion message.

The final public payload may exclude a component if its source/license review
does not pass. This list is not a redistributable-binary inventory.

Private SM64 0.6 development adds a Start+X options menu with quality, scaling,
volume, resume and exit controls. Host and MIPS private validation passed,
including settings/save persistence and frozen game logic while the menu is
open. Physical 0.6 menu acceptance remains pending; these notes do not declare
the public port finished, redistributable or performance-certified.

## Evidence Levels

| Area | Current evidence | Important boundary |
| --- | --- | --- |
| M8 UI and game launch | Owner physical tests | Exact tested firmware lineage only |
| SNES, GBA, Mega Drive | Owner reports working gameplay and sound | Not every game/core combination tested |
| PCSX4ALL | Owner reports good gameplay; app migration tested | ROM/BIOS excluded; final binary provenance requires audit |
| RK707 X/PS/D modes and RetroArch analogs | Owner physical confirmation | Other controllers are not universally certified |
| Power Off completion display | Owner physical confirmation | Cannot protect against unplugging during writes |
| Themes | Parser/rendering and workflow tests | Partial ES compatibility, not pixel equality for all themes |
| Media players | Host/sanitizer and MIPS Explorer handoff tests | Native M8 sound and demanding-media performance unverified |
| Device Adapter | Windows/Linux tests; native Windows GUI inspection | Native macOS and newly adapted card boot unverified |
| Original SM64 | Private full-engine host/MIPS tests and owner gameplay | Not a public BYO package; renderer/assets block distribution |

## Known Issues And Exclusions

- General N64 emulation and PCSX ReARMed can be slow or inaccurate on this
  CPU-only device. PCSX4ALL is the tested PSX alternative.
- Legacy OpenDingux OPKs are not universally binary-compatible. Use qualified
  source rebuilds, not arbitrary uClibc/glibc or GPU-dependent binaries.
- Heavy themes and high-bitrate/high-resolution media may exceed CPU/RAM limits.
  Theme video regions use still artwork; animated images show their first frame.
- Wi-Fi, Bluetooth, USB gadget sharing, ZRAM, frequency changes and OTA depend
  on board/kernel support. A visible menu item does not prove M8 availability.
- FAT storage is vulnerable to power loss and faulty readers. Backups and safe
  shutdown remain necessary; a successful flush is not a card health guarantee.
- The current stock-preserving private image contains dependencies whose
  redistribution clearance has not been established. Do not upload it.
- The Device Adapter installs only the approved exact M8 firmware profile.
  Unknown signatures are inspect/export-only; it cannot reconstruct a boot gap
  from a folder or turn an arbitrary image into universal firmware.

See the [publication checklist](PUBLICATION-CHECKLIST.md) for remaining release
work and the [troubleshooting guide](troubleshooting.md) before reporting a bug.
