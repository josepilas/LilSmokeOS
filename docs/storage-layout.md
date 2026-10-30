# LILOS and LILROMS (layout version 2)

Exactly two primary MBR partitions, with 512-byte logical sectors:

| Partition | Label | Format | Contents / mount |
| --- | --- | --- | --- |
| 1 | LILOS | FAT32, 512 MiB | OS payload files, /lilos |
| 2 | LILROMS | exFAT by default, FAT32 accepted | ROMs, BIOS, themes, ports, /storage |

LILOS starts at sector 2048; LILROMS starts at sector 1050624. The distributed
storage image has a 127 MiB ROM seed volume. At first boot it grows to the final
sector of the containing card. Partition 1 never moves or grows. The unused
alignment prefix is not a third partition or a validated bootloader installation.
This MBR implementation supports cards below 2 TiB, not GPT or 4K-sector devices.

An ext4 **file**, /lilos/lilsmoke/persist.ext4 (64 MiB), is loop-mounted at
/persist. This preserves permissions, SSH keys, overlayfs and symlinks without
creating another partition. LILOS has private mount permissions. Reformatting
LILROMS does not erase these OS settings. exFAT and FAT32 do not support POSIX
symlinks: PortSmoke's existing save-bind fallback remains necessary.
FAT32 additionally limits each individual file to less than 4 GiB.

## First boot and recovery

1. Identify one FAT32 LILOS partition and validate its layout contract and
   companion partition. Ambiguous disks and unrecognised filesystems are refused.
2. Before any resize/format, check the unmounted LILROMS filesystem, stream a
   complete source-volume backup to LILOS, check that backup, and store its SHA256.
3. Commit a journal before modifying partition 2. Only its MBR entry changes;
   BLKPG updates the kernel's partition geometry without rereading mounted p1.
4. Format the enlarged p2 in the **same filesystem type**, restore every file
   and empty directory, compare contents, sync, unmount, and check the filesystem.
5. Commit completion before deleting the journal and backup. A failed restore
   is replayed from the verified source on the next boot.

This is a backup/recreate/restore transaction, NOT an in-place exFAT resize.
It uses streaming I/O and a disk-backed backup, not a 127 MiB RAM buffer.
Low backup space, a corrupt source/backup, busy mount, changed recovery geometry
or export ownership stops the operation. The backup and journal remain available
for recovery. FAT32 does not provide a journal for LILOS itself, so this is not a
guarantee against arbitrary power loss or SD-controller failure. Keep power
connected during provisioning, and keep an independent backup of valuable data.

The default image's fixed-size seed bounds the space needed on LILOS. A manually
enlarged, nonempty seed that no longer fits the backup must be backed up and
provisioned separately; the system never silently erases it.
Manual FAT32 formatting is detected by filesystem type even if the label was
removed. The system does not require switching a setting.

## LILDRIVE

The ConfigFS USB **product** remains LILDRIVE. Only the validated p2 block device
is exported. The entire SD disk, LILOS and persist.ext4 are never exported.
Windows may display the filesystem's LILROMS volume label rather than the USB
product name. Keeping LILROMS as its filesystem label is intentional.

The console unmounts /storage before export. Nested mounts, active .peng loop
images and a busy filesystem prevent export; there is no forced/lazy unmount.
A root-owned transfer marker prevents new emulator launches and Explorer edits.
The host owns the volume until the gadget unbinds AND its backing device detaches.
Safely eject on the PC before disabling sharing. Local remount includes an
offline filesystem check. 9p shared-folder and RAM development backends cannot
be exported as a fake LILROMS block partition.

## Build and test boundary

scripts/mk-sdcard.sh accepts a NEW regular output file, --development, a board,
and a payload via --rootfs. It refuses raw-device and guessed U-Boot writes.
The output tests storage; it is **not a production-bootable HC16xx firmware**.
The real console boot/display/audio BSP still needs physical verification.
A file-based boot/rollback adapter is required before OTA install/activation
can be enabled; the old p2/p3 formatter has been removed.

The builder now accepts --rootfs-format squashfs and an optional --kernel ELF
payload. This stores rootfs.squashfs and vmlinux as separate files under
/lilos/lilsmoke/slots/A, with SHA256SUMS. The default fit option retains the
older lilsmoke.itb path. These filenames do not create a boot adapter.
--board hc16xx-common denotes a development storage layout, not approval of
every HC16xx board. No vendor bootloader, ROM, BIOS or publisher key is bundled.

The desired distribution is a raw sector image (.img), not an ISO and not a
standalone .fat file. The image already contains both filesystem volumes and
their MBR entries. Write it as a disk/DD image in Rufus, without reformatting
or replacing its partition table. Before flashing, back up the card and verify
the correct target device. Do not flash the development image to a console as
if its currently unverified boot chain were supported. See
m8-tv-common-image-study.md for the production gating and main.syspck flow.

Run real disposable tests (Linux root, private mount namespace):

~~~sh
bash tools/hwtest/lildrive_filesystem_smoke.sh
bash tools/hwtest/validate_storage_mips.sh
python3 tools/hwtest/usb_service_smoke.py
~~~

No physical SD card is modified by those tests. See test-results/storage-layout.
The GNU Parted manual explains that changing a partition end does not resize
its filesystem: https://www.gnu.org/software/parted/manual/html_node/resizepart.html
The upstream exfatprogs resize request remains open:
https://github.com/exfatprogs/exfatprogs/issues/134
