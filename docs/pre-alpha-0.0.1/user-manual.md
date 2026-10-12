<!-- SPDX-License-Identifier: MPL-2.0 -->
# User Manual

This guide covers the audited pre-alpha development UI. Availability depends
on installed providers and board support; see [release status](RELEASE-NOTES.md).

## Home And Games

Navigate with the D-pad/left stick and select with A. B returns. Choose a
console, choose a ROM, then launch it. **OpenDingux** opens installed native
applications' own menus; the console's ROM list instead launches its selected
provider directly. **LilSmoke Applications** is a separate, theme-independent
grid of installed `.lilpck` applications.

After copying ROMs, use **Settings > Scan games now**. Enable **Scan ROMs on
boot** for future boots. Missing content can also mean an unsupported extension,
wrong system folder or absent emulator, not just a stale scan.

**Start+X** requests an emulator menu; **Start+Select** exits to the launcher.
These are integrated emulator bindings, not a guarantee that every third-party
native application handles them identically. Save in-game and/or use the
emulator's supported save controls before exiting.

## LilSmoke Explorer

Open Explorer to manage `/storage` (LILROMS). Normal browser controls:

| Control | Action |
| --- | --- |
| Up/down | Select entry |
| A, Start or right | Open file/folder |
| B or left | Parent folder |
| X | File actions |
| Y | Mark/unmark entry |
| Select | Find/filter |
| Shoulder buttons/triggers | Page through entries |

Actions include Copy, Move, Paste, Rename, Delete, New folder, New file,
Properties, Refresh and supported archive extraction. Marked items enable
batch operations. Move is queued until Paste. Confirm overwrites/deletions
carefully: there is no general recycle-bin guarantee.

Open `.lilpck` files to install [applications](APPLICATIONS.md). Open supported
media to launch the [player](MEDIA.md), which returns to the same Explorer
directory/file on exit. RAR/ZIP/7z/tar-family extraction depends on the compiled
archive backend; encrypted, split, oversized or malformed archives may fail.
Do not use extraction to install untrusted programs or to edit boot files.

## Settings

Use left/right to change enumerated values and A to enter action pages. Useful
items include **Emulator Core Settings**, **Scan games now**, **Master volume**,
**Theme**, **Theme system layout**, **Theme game list**, **Gamepad mapping**,
**Mouse / touchpad**, **Pointer speed** and **Cursor size**.

For TV use, select the TV presentation and an appropriate safe margin for your
display. TV output is 16:9; games can retain their original 4:3 aspect. Stretching
a game's pixels is not increased rendering detail. Avoid globally changing
emulator scaling until you know which provider owns its display settings.

ZRAM, governors, overclocking, Wi-Fi, USB modes, Bluetooth and OTA require
services/kernel capabilities absent on some devices. Leave stock frequency
and unsupported options alone; this edition does not certify these on the M8.

## Power Off

1. Save your game and return to the launcher. Finish installs/transfers.
2. If USB storage sharing is active, safely eject it on the PC and turn sharing
   off first. The console must regain exclusive ownership of storage.
3. Choose **Settings > Power Off** and confirm; Cancel is the safe default.
4. Wait for **Power off operation succeeded** to remain visible for ten seconds,
   then for the screen to go black. Only then disconnect the M8 power cable.

The M8 cannot electrically cut its power. The operation closes writers,
flushes storage and unmounts or makes relevant mounts read-only before halting.
**Storage closed but SD needs repair**, **STORAGE BUSY WAIT**, an error, or black
screen alone is not the success confirmation. Record the warning and consult
[troubleshooting](troubleshooting.md). Do not repeatedly pull power to diagnose it.

Safe shutdown reduces risk; it cannot guarantee recovery from failing hardware,
an already damaged filesystem or power loss during writes. Safely eject both
partitions on your PC too. Keep independent backups of saves and settings.
