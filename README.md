# LilSmokeOS

**Pre-Alpha-0.0.1**. Created and led by **José Pilas**.

LilSmokeOS is a Buildroot external tree and controller-first Linux interface
for constrained MIPS devices. Initial releases use a personal-use proprietary
license for original material owned by José Pilas. The main authored OS source
repository remains private in initial versions for personal reasons; a future source
release is intended to use MPL-2.0. Upstream components retain their own terms
and source/notice obligations. Public SM64-LilSmoke adaptations use MPL-2.0.

## Release Status

The experimental [Pre-Alpha-0.0.1 image](https://github.com/josepilas/LilSmokeOS/releases/tag/Pre-Alpha-0.0.1)
is published. It is **not certified universal firmware or complete redistribution
clearance**. Owner-imported boot dependencies and complete third-party source,
build/relinking and resource notices still have documented audit gaps.
Private SM64 packages embed extracted game data and a source-only renderer and
must not be attached to a release. See [licensing](LICENSE_POLICY.md).

The physically tested device is the owner's **SEGAM-M8-V6.0**, stock firmware
**V1.0.0 2024-12-05**, HCSEMI MIPS/128 MB lineage. Other M8, SF3000 and GB350
boards are not certified by that result. Never select firmware solely by a
console's marketing name.

## Guides

- [Pre-alpha guide](docs/pre-alpha-0.0.1/README.md)
- [Installation](docs/pre-alpha-0.0.1/installation.md)
- [User manual](docs/pre-alpha-0.0.1/user-manual.md)
- [Troubleshooting](docs/pre-alpha-0.0.1/troubleshooting.md)
- [Developer guides](docs/developer/README.md)
- [Credits](CREDITS.md), [third-party notices](THIRD_PARTY_NOTICES.md)

Two visible partitions are used: **LILOS (FAT32)** and **LILROMS (FAT32/exFAT)**.
The tested spare uses FAT32 for both. LILROMS stores games, themes, applications
and user data and is the only partition eligible for LILDRIVE export.
Use Settings -> Power Off and wait for the success message/black screen before
unplugging the M8. A black TV alone does not prove that storage closed safely.

## Experience

LilSmokeUI integrates controller input, theme import, ROM scanning, per-system
emulator selection, LilSmoke Explorer, music/video/photos and the independent
Applications `.lilpck` installer. Applications bundle their runtimes and can
declare `open_core` associations. RetroArch and adapted OpenDingux applications
are integrated; PCSX4ALL is an independent application.

Owner tests confirm SNES/GBA/Mega Drive/PCSX4ALL gameplay, M8 sound, emulator
menu/exit combos, shutdown and RK707 D/PS/X-input. This is not a guarantee for
every game, controller, codec or board. Networking, Bluetooth, USB gadget modes
and ZRAM depend on the actual kernel/hardware. N64/3D and HD video are CPU-limited.

The PC Device Adapter reads a stock-folder backup, creates a private firmware
export and applies a reviewed matching adaptation with verified backup/recovery.
Its initial install profile matches only the tested M8 lineage; unknown variants
are inspection/export-only. Windows has an executable; Linux/macOS launchers
require Python 3.10+ and Tk. A folder cannot supply an incompatible boot gap.

## Architecture

```text
UI / Explorer / Applications / Media
          | low-memory process handoff
RetroArch / adapted OpenDingux / independent apps
          | software SDL / DirectFB / native M8 adapters
musl userspace + BusyBox services
          | board-specific boot/kernel contracts
Validated MIPS board, or Malta/QEMU development fixture
```

No GPU/GL/GLES/Vulkan is assumed. M8 framebuffer scaling is a 2D presentation
facility, not hardware 3D. QEMU tests ABI/control workflows and bounded memory;
it does not reproduce HC16xx cycle timing or audible auddec.

## Development

Use the documented pinned Buildroot/SDK and board workflow, not an arbitrary
upstream kernel or ARM image. QEMU is a development fixture:

```sh
make BR2_EXTERNAL=/path/to/lilsmoke-os lilsmoke_qemu_mipsel_defconfig
make
/path/to/lilsmoke-os/scripts/run-qemu.sh output/images
```

Read [the publishing checklist](docs/release-preparation.md) before staging.
Never `git add .` from this development workspace: stock exports, private
packages, ROMs, build outputs and captures are outside release scope.
Repository: [josepilas/LilSmokeOS](https://github.com/josepilas/LilSmokeOS).
The published prerelease contains only the frozen OS `.img` as its release
asset; authored OS source is not published here. Read the release's exact
verification limits and third-party source/notice gaps before using it.

