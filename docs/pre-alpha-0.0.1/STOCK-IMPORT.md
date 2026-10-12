<!-- SPDX-License-Identifier: MPL-2.0 -->
# Import Original Card Files

The **LilSmoke Device Adapter** reads an extracted stock-card folder, exports
allowlisted dependencies into a private PC folder, then adapts a different,
already flashed compatible LilSmoke card. Source files remain read-only.
It does not flash, format, repair, resize or recover a raw disk image.

## Start The PC Tool

Copy `LILOS/PC-Tools/LilSmoke Device Adapter` to your PC before running it.
Do not run the tool from the target card while installing onto that same card.

| Host | Launcher | Requirements / evidence |
| --- | --- | --- |
| Windows x64 | `LilSmoke Device Adapter.exe` | Bundled runtime; built/tested on Windows |
| Linux | `sh "LilSmoke Device Adapter.sh"` | Python 3.10+ and Tk; Linux tests passed |
| macOS | `sh "LilSmoke Device Adapter.command"` | Python 3.10+ and Tk; native macOS unverified |

Linux/macOS launchers are not standalone bundled native applications. A macOS
`.command` is not a notarized `.app`. CLI operations require Python but not Tk.

## Inspect, Export, Review, Install

1. Select the recovered original card root containing `cubegm`, or select the
   `cubegm` folder itself. Inspection compares identifiers/hashes without
   executing vendor programs. Keep the original card/backup intact.
2. Select a new private export directory on the PC, outside the source. Export
   copies only allowlisted firmware dependencies and verifies them. ROMs, BIOS
   collections, saves and firmware flashers are not copied into that export.
3. Remove the original card. Insert a different card already flashed with the
   compatible LilSmoke base. Select its mounted **LILOS FAT32 volume root**, not
   LILROMS, the whole disk, a subfolder or your PC's system volume.
4. Review the changed paths, hashes and space requirement. Install checks the
   reviewed plan again, creates verified rollback data, stages/verifies files
   and commits the launch hook last. Existing LilSmoke runtime is not replaced.
5. Wait for completion, safely eject both partitions and boot the target device.
   Check display, sound, input, games, saves and Power Off before trusting it.

The initial installer profile recognizes the exact tested SEGAM-M8-V6.0
firmware lineage. Unknown or mismatched signatures are **inspect/export-only**.
Matching hashes do not certify another PCB. The tool refuses missing/ambiguous
disk identity, overlapping source/target, unsafe links and unsupported mounts.
It does not write LILROMS or copy a source MBR onto the target.

## CLI Alternative

From the copied tool directory:

```sh
python3 launch.py inspect /path/to/stock
python3 launch.py export /path/to/stock /path/to/new-private-export
python3 launch.py plan /path/to/private-export /path/to/LILOS
python3 launch.py apply /path/to/private-export /path/to/LILOS --approve-plan HASH_FROM_REVIEW
python3 launch.py rollback /path/to/LILOS
```

The Windows executable accepts the same subcommands. Replace placeholders with
your paths and the reviewed plan hash; do not invent a hash or force a refusal.

## Recovery And Privacy

Transactions are recoverable, **not power-loss-atomic**. An unfinished operation
blocks another install until recovery. Keep its backups/journal at
`LILOS/lilsmoke/device-adapter/transactions` until physical boot succeeds.
Recover/Rollback validates the restore set and refuses unrelated later changes.
Filesystem corruption still requires separate recovery outside this tool.

A folder cannot supply the reserved boot gap, bootloader signatures or unknown
board drivers. No guessed DTB or `.syspck` bypass makes arbitrary devices work.
Never upload private firmware exports, original-card images or identifying disk
serials to GitHub. Share only redacted diagnostic summaries when requesting help.
