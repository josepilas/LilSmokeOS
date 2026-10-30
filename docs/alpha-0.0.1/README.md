<!-- SPDX-License-Identifier: MPL-2.0 -->
# LilSmokeOS Alpha-0.0.1

Released October 30, 2026. Created and led by **José Pilas**.

This is the updated system developed after Pre-Alpha-0.0.1, not simply a
change to source availability. Start with the [complete release
notes](RELEASE-NOTES.md) and [current user guide](USER-GUIDE.md).

## Downloads

Get the image, source and checksums from
[Alpha-0.0.1 Releases](https://github.com/josepilas/LilSmokeOS/releases/tag/Alpha-0.0.1).

| Artifact | Purpose |
| --- | --- |
| `LilSmokeOS-Alpha-0.0.1.img` | Raw flashable SEGAM-M8-V6.0 image |
| `LilSmokeOS-Alpha-0.0.1-source.zip` | Audited source snapshot at initial publication |
| `SHA256SUMS.txt` | Image/source SHA-256 checksums |
| `LilSmokeOS-Alpha-0.0.1-documentation.zip` | Corrected Alpha documentation, published separately after the initial snapshot |

The documentation correction does not replace the image, change its checksum,
move the release tag, or alter a connected card. The original source ZIP and
its inventory describe the original publication snapshot; corrected docs are
on the repository's `main` branch and in the documentation ZIP.

## Supported Device

The image targets the tested **SEGAM-M8-V6.0**, stock firmware
**V1.0.0 2024-12-05**, HCSEMI MIPS/128 MiB lineage. Its userspace has no
hardware 3D requirement. The M8 uses native framebuffer/PCM adaptations;
it is not an Ingenic JZ4770 board with ordinary ALSA devices.

TV defaults are 1280x720 with a 5% safe margin. This is not universal firmware
for every M8, R36SX, SF3000 or GB350 revision. A matching name or CPU is not a
sufficient compatibility check.

## Flash And First Boot

1. Preserve the original working card. Back it up and use a spare card.
2. Verify the downloaded image against `SHA256SUMS.txt`.
3. Use Rufus or another raw-image writer to flash the `.img` to the correct
   card. Flashing destroys the selected destination's previous contents.
4. Safely eject it, insert it into the supported device and boot. Allow
   LILROMS expansion and any automatic restart to finish without cutting power.
5. Copy your games to the matching `LILROMS/roms/<system>/` folders. Run
   **Settings -> Scan games now** after adding content.
6. Use **Settings -> Power Off** and wait for the completion indication and
   black screen before disconnecting power.

The image has LILOS **1 GiB/FAT32** and an initial LILROMS **768 MiB/FAT32**.
LILROMS expands on first boot; its FAT capacity is prepared for cards up to
128 GiB. General exFAT support in the tree does not mean that exFAT has been
validated on this stock M8 kernel. Do not format a working card unnecessarily.

## Guides

- [What changed since Pre-Alpha](RELEASE-NOTES.md)
- [Controls, games, applications and troubleshooting](USER-GUIDE.md)
- [Theme support and remaining gaps](../theme-compat.md)
- [Emulator selection, archives and save states](../cores.md)
- [OpenDingux integration](../opendingux-integration.md)
- [LilSmoke Applications](../lilsmoke-applications.md)
- [TreeFrog compatibility and optional conversion](../treefrog-port-converter.md)
- [SDK/API](../lilsmoke-applications-sdk.md)
- [Developer guides](../developer/README.md)
- [Source inventory and build limits](../alpha-0.0.1-source.md)
- [License policy](../../LICENSE_POLICY.md)

## Validation

Fifty Linux release tests passed. A one-CPU, 128 MiB MIPS VM exercised
first-boot expansion, LilSmokeUI, SNES launch from a 7z archive, Start input,
and clean filesystem closure. Another extended run reached the emulator
menu and returned to the UI, but its shutdown assertion did not pass:
the production power guard requires the real HC framebuffer.

These tests do not measure physical M8 FPS or validate its audio hardware.
Earlier owner tests cover device functionality, but this exact clean Alpha
image has not received a new physical cold-boot acceptance test.

## Distribution Scope

Authored LilSmokeOS code is MPL-2.0. Third-party components retain their own
licenses. Vendor boot/BSP materials are not relicensed, and exact corresponding
vendor source remains incomplete. Read the notices before redistribution.

No ROMs, BIOS collection, user saves or private game data are supplied.
Daedalus-LilSmoke and the custom Mupen-LilSmoke experiments are excluded.
SM64-LilSmoke, Undertale-LilSmoke, the converter and the SDK have separate
repositories/releases; their existence does not mean their game data or
development toolchains are bundled in this image.
