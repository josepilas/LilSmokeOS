<!-- SPDX-License-Identifier: MPL-2.0 -->
# Installation And Updates

**These are draft instructions for a future approved image, not permission to
flash an existing private Candidate build.** There is currently no public
download/checksum declared here. Read [release status](RELEASE-NOTES.md) first.
The owner's [GitHub release index](https://github.com/josepilas/LilSmokeOS/releases)
is the future download location; no version-specific image link is supplied.

## Before Flashing

1. Identify the board, firmware version and stock boot-file profile. Do not
   assume every M8/V6, SF3000, GB350 or R36-branded product is the same hardware.
2. Preserve your original working card. Make a whole-card backup where possible
   and a separate copy of readable files. A folder lacks the raw boot region.
3. Use a different, empty or expendable microSD card. Flashing overwrites its
   partition table and files. Back up anything valuable on that target too.
4. Obtain only the image and SHA256 published for your approved profile. Check
   the hash before writing and confirm reader identity/capacity, not just a
   reusable drive letter or `/dev/sdX` name.

Example checksum checks, with placeholder paths:

```powershell
Get-FileHash -Algorithm SHA256 "C:\Downloads\LilSmokeOS-Pre-Alpha-0.0.1.img"
```

```sh
# Linux
sha256sum LilSmokeOS-Pre-Alpha-0.0.1.img
# macOS
shasum -a 256 LilSmokeOS-Pre-Alpha-0.0.1.img
```

Compare the complete hash with the release checksum; a filename is not proof.

## Write The Image

Use a disk-image writer that preserves the supplied sector layout. On Windows,
use Rufus in **DD/raw-image mode**, not an ISO conversion or a filesystem copy.
Consult the [Rufus FAQ](https://github.com/pbatard/rufus/wiki/FAQ) for its
image/device support and safety caveats.
On Linux/macOS, use a suitable raw-image GUI writer. Inspect its target selection
and wait for write/verification to finish. If the tool cannot recognize the
image, stop rather than formatting partitions to make it appear compatible.

The flashing operation should produce exactly two partitions:

| Label | Filesystem | Purpose |
| --- | --- | --- |
| LILOS | FAT32 | Boot dependencies, OS runtime, settings support, PC tools |
| LILROMS | FAT32 or exFAT | ROMs, BIOS, themes, applications and user files |

The intended default is exFAT for LILROMS, but the physically tested M8 card is
**FAT32**. The image's manifest must state its actual format and partition sizes.
Generic exFAT support is not proof that the stock M8 kernel can mount exFAT.
Do not reformat LILOS or relocate either partition after flashing.

FAT32 cannot hold an individual file of 4 GiB or larger. LILROMS mounted on the
console is `/storage`; copying to `LILROMS/roms/snes` therefore creates
`/storage/roms/snes`. OS updates must preserve ROMs, saves and settings.

## Adapt And First Boot

If the published profile requires local stock files, follow the
[Device Adapter procedure](STOCK-IMPORT.md) before booting. A compatible base
boot region is still required; the importer is not a flasher.

Safely eject both volumes, insert the spare card, connect the controller and
boot on stable power. First-boot expansion is profile-dependent: when enabled,
LILROMS consumes the remaining card space and may require a restart. Do not
disconnect power during this process. This implementation can back up, recreate
and restore a seed filesystem; it is not universally an in-place resize.
An error/insufficient-backup-space condition must stop expansion, not erase data.

After boot, check the UI, controller, one legally obtained game, sound, saving,
menu/exit chords, and safe shutdown. Record the actual device/profile tested.

## Updating An Existing Card

Use only the update method documented alongside the eventual public release.
There is **no universal supported public OTA procedure in this draft**. Internal
Candidate deployment scripts are maintenance tools with device-specific guards,
not end-user installers. Do not bypass their identity or layout checks.

Back up both volumes and verify the backup before updates. Do not raw-flash an
existing card expecting ROMs/settings to survive. Native application updates
are separate: open a trusted `.lilpck` in Explorer and confirm INSTALL.

If Windows reports RAW or requests repair, cancel any format prompt. RAW may
indicate an invalid layout or actual corruption; it does not by itself diagnose
the boot chain. Disconnect safely and recover from backup before trying repairs.
See [troubleshooting](troubleshooting.md).
