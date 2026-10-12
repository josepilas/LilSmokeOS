<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmokeOS Pre-Alpha-0.0.1

**Release documentation draft. No public image release is declared by these
documents.** Publication must pass the [release checklist](PUBLICATION-CHECKLIST.md).
Private development images are not public download artifacts.

Project links: [repository](https://github.com/josepilas/LilSmokeOS),
[issues](https://github.com/josepilas/LilSmokeOS/issues),
[release index](https://github.com/josepilas/LilSmokeOS/releases).
The release index is not a claim that a Pre-Alpha-0.0.1 tag/image exists.

LilSmokeOS is a lightweight Linux frontend, emulator integration and native
application environment created and led by **José Pilas**. This edition's
physical development baseline is the **SEGAM-M8-V6.0, firmware V1.0.0
2024-12-05** in the HCSEMI/CubeGM MIPS family. The marketing name alone is not
enough to identify a compatible board or firmware.

## Read First

- [Release notes and known issues](RELEASE-NOTES.md)
- [Installation, storage and updates](installation.md)
- [Importing your original card files](STOCK-IMPORT.md)
- [User manual and safe shutdown](user-manual.md)
- [Games, emulators and compressed ROMs](EMULATION.md)
- [Controllers, keyboards and pointers](INPUT.md)
- [LilSmoke Applications](APPLICATIONS.md)
- [EmulationStation themes](THEMES.md)
- [Music, video and photos](MEDIA.md)
- [Network access and USB sharing](NETWORK.md)
- [Troubleshooting](troubleshooting.md)
- [License scope and redistribution](LICENSES.md)
- [Developer guides](../developer/README.md)
- [Credits](../../CREDITS.md)

## Hardware Boundary

The tested M8 has one MIPS 74Kc CPU around 1.2 GHz, 128 MiB installed RAM
(roughly 110 MiB reported usable), a vendor Linux 4.4.186 kernel, and native
framebuffer/audio interfaces. It has **no usable hardware 3D acceleration**.
LilSmoke's musl userspace uses SDL software rendering and the board's 2D
display scaler. HDMI output size does not equal game rendering resolution.

The earlier Ingenic JZ4770/SF3000/GB350 plans are not evidence that this release
boots on those devices. QEMU Malta tests check MIPS code and workflows; they
are not cycle-accurate M8 hardware emulation or proof of physical FPS/audio.

## Data And Safety

Use a spare card and keep an independent backup of your working stock card.
No ROMs, BIOS files, commercial game assets or private stock exports belong in
the public release. Native applications and packages are trusted code, not a
sandbox. Use **Settings > Power Off**, wait through the ten-second success
message and subsequent black screen, then disconnect power.

These guides describe the audited development baseline on 2026-10-12. The
final release inventory, image checksum and source tag must be supplied by the
release audit, not inferred from old Candidate filenames or milestone numbers.
