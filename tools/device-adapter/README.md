# LilSmoke Device Adapter 1.0.0

A PC utility supplied in `LILOS/PC-Tools/LilSmoke Device Adapter/`.
It reads **a folder of original stock-card files**, exports only allowlisted
private firmware dependencies, and adapts a **different, already flashed**
LilSmoke card. It never flashes, formats, repairs or resizes a disk.

## Run

- Windows x64: double-click `LilSmoke Device Adapter.exe`. No Python or WSL
  installation is needed for this executable. The console is retained for CLI
  output; a native Tk window opens when launched without arguments.
- Linux: `sh "LilSmoke Device Adapter.sh"`. Python 3.10+ and Tk are required
  (for example `python3-tk` from the distribution). CLI needs only Python.
- macOS: run `sh "LilSmoke Device Adapter.command"`, or double-click after
  making it executable. Use Python 3.10+ with Tk, such as the python.org installer.
  The script is not a notarized native `.app`.

Copy the PC-tool folder from LILOS to your PC **before** adapting that same card.
Running the executable from the target card is deliberately unsupported while
its files are being modified. Keep private exports on the PC, not LILOS.

## Workflow

1. Browse to the recovered original card root (containing `cubegm`) or `cubegm`
   itself. Inspect reads only twelve identifying boot/library dependencies.
2. Select a new export folder outside the source. Export copies a bounded,
   allowlisted set, flushes and rereads every file, then writes the completion
   marker last. Stock ROMs, BIOS collections, saves and update flashers are not
   copied. Exported vendor files are private: do not distribute/upload them.
3. Remove the original card. Insert a different card already flashed with the
   compatible LilSmoke base. Select its mounted **LILOS FAT32 volume root**.
   LILROMS stays FAT32/exFAT and is never written by the adapter.
4. Review Install shows exact changed files, existing/new hashes and space
   requirements. Install checks the reviewed plan again, creates verified
   rollback backups, stages all changes, verifies runtime identity, then installs
   them. The `icube` launch hook is committed last; original `icube` is kept as
   `icube.stock`. The existing LilSmoke runtime is not replaced.
5. Safely eject both partitions using the OS, then physically test boot, display,
   sound, controller modes, emulators, saves and safe shutdown. The report always
   states that this particular installation has not been physically boot tested.

Unknown/mismatched firmware can be inspected and privately exported but **cannot
be installed**. The initial profile matches only the owner's exact SEGAM-M8-V6.0
stock dependency hashes, not arbitrary devices sharing that marketing name.
Identical fingerprints mean a matching firmware lineage, not certification of
another PCB. There is no automatic `.syspck` signing or hardware guessing.

## Command Line

```text
python3 launch.py inspect /path/to/stock
python3 launch.py export /path/to/stock /path/to/new-private-export
python3 launch.py plan /path/to/private-export /path/to/LILOS
python3 launch.py apply /path/to/private-export /path/to/LILOS --approve-plan HASH_FROM_REVIEW
python3 launch.py rollback /path/to/LILOS
python3 launch.py rollback /path/to/LILOS --transaction TRANSACTION_ID
```

The Windows executable accepts the same commands without `python3 launch.py`.
JSON reports go to stdout; refusal messages go to stderr with exit status 2.
There is no CLI override for an unknown family, profile, system disk or unsafe
target. Maintainer tests inject a synthetic identity only through Python APIs.

## Interrupted Operation

Do not unplug during writes. No filesystem provides atomic replacement of this
entire set of FAT files. Transactions are recoverable, **not power-loss-atomic**.
Backups and journals are under `lilsmoke/device-adapter/transactions/ID` on LILOS.
An `active` marker blocks another installation until recovery. Recover / Rollback
verifies the complete restore set first, refuses unrelated subsequent changes,
then restores the previous hook before other files. Completed transactions can
also be rolled back by selecting their journal. Keep backups until a physical
boot succeeds. This tool does not run CHKDSK/fsck after a disconnect or corruption.
If the filesystem itself is damaged, recover it outside this tool first.

## Limits

- Input is an extracted regular folder, **not** a raw image/physical disk reader.
  A folder does not contain the reserved boot region. The base image must already
  have a compatible bootloader/boot gap; the adapter cannot make an arbitrary
  universal image boot on an unsupported board.
- Exact vendor hashes, recognized layout, reviewed launch-script hash and base
  image provenance are required. Kernel/DTB aliases remain byte-identical.
- Physical disk identity and an external mounted FAT32 LILOS root are required.
  Ambiguous virtual/network mounts, system disks, overlapping paths, links,
  junctions and the same physical source/target disk are refused.
- Windows uses read-only Storage cmdlets for whole-disk identity; Linux uses
  mountinfo/sysfs/lsblk; macOS uses `diskutil info -plist`. Platform failures stop
  installation rather than falling back to path-only checks.
- Windows USB, SD and MMC buses are accepted only when the target is non-system,
  writable, labelled correctly and has exactly two partitions. macOS requires
  a whole-media UUID/hardware serial, partition offset/size and whole-disk size;
  Linux requires a sysfs serial/CID and partition geometry. A reusable `diskN`
  or `/dev/sdX` name alone cannot authorize installation. Readers without usable
  identity stay inspect/export-only. Using the recovered folder on the PC avoids
  confusing two cards swapped through a reader with the same hardware identity.
- Firmware files are bounded to 64 MiB each and 512 MiB total. Metadata is bounded.
  Network protocols, executable stock programs and shell evaluation are not used.
- Windows executable is built/tested here. Linux CLI is tested here. Native
  macOS GUI/volume probing and physical adapted-card boot require platform/device
  testing; do not interpret synthetic tests as those certifications.

The public delivery includes `third_party/` runtime notices, also embedded in
the Windows executable. No private export or test firmware belongs in LILOS's
PC-tool folder. Application callbacks are tested directly in Tk using synthetic
folders; no keyboard or mouse events are injected into Windows.

## Maintainers

`build_profile.py` reuses the project's existing stock allowlist read-only and
produces **hashes only** after verifying the Candidate06 gate. Rebuilding the
profile is a reviewed maintenance operation, not an end-user bypass.
`build_windows.ps1` freezes the standard-library tool with existing PyInstaller.
`prepare_delivery.py NEW_FOLDER` prepares a local LILOS payload only; the parent
deployment must review and copy it. No proprietary firmware belongs in that
payload. Run `python -m unittest discover -s tests -v` before release.
