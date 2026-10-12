<!-- SPDX-License-Identifier: MPL-2.0 -->
# Board And Stock-Profile Porting

An approved profile needs hardware evidence and a working boot chain, not just
a retail name, MIPS ELF or similar `dtb.bin`. The tested HCSEMI M8 is not the
earlier Ingenic JZ4770 plan. Treat each PCB/firmware lineage as a separate target.

## Investigate Read-Only

Record PCB/SoC markings, CPU count/ISA/endianness/float ABI, usable RAM, flash/card
geometry, stock firmware version, bootloader/kernel/DTB format, display/audio
interfaces and USB/input capabilities. Preserve a full stock image and original
card before changes. A folder loses reserved boot bytes; Windows RAW status is
not enough to classify the layout or bootability.

Use independent source/hardware evidence, retain hashes/provenance and do not
execute unknown vendor flashers. Distinguish a Linux DTB from vendor-specific
board/configuration blobs. `.syspck` is a project configuration container, not
a universal kernel/bootloader translator. A single public image across families
remains a goal, not permission to reuse incompatible boot regions.

## Define Interfaces And Budgets

For the reference M8, investigate the native hcfb 2D scaler, auddec PCM worker,
vendor controller paths, old-kernel evdev ABI and unavailable syscalls. Do not
assume ALSA, ConfigFS gadgets, loop/FUSE, clock controls or modern kernel HID
drivers exist. A loader symlink cannot make uClibc/glibc applications musl-safe.

Bound video/input/audio buffers and archive memory. Keep native game surfaces
small; use the display scaler rather than rendering 3D at HDMI resolution.
Avoid GPU APIs and external input daemons. Review storage ownership during
LILDRIVE export and keep safe shutdown independent of an SD-backed executable.

## Add An Adapter Profile

The Device Adapter uses allowlisted files, exact reviewed dependency hashes,
launch-script identity and a compatible target layout/provenance. Unknown
signatures remain inspect/export-only. To extend it, add a reviewed profile,
synthetic fixtures and platform identity tests, then independently validate a
spare adapted card. Do not provide a force/ignore-hash option for user installs.

Transactions need verified rollback, changed-file inventories, completion
markers and interruption tests. Source must stay read-only; source/target
overlap and ambiguous disks must be refused. The PC tool never repairs,
repartitions, flashes or edits LILROMS. Raw image construction is separate.

## Acceptance Before Shipping

Require physical cold boot, correct TV/panel output, native sound, controller
modes/analogs, storage scan, compressed/plain games, saves, own emulator menus,
clean return and ten-second safe shutdown confirmation. Test first-boot
expansion and recovery on disposable media. Check readonly source preservation
and final card readback. Confirm no unsupported clock/thermal changes.

Record failures and tests not performed. QEMU workflow passes and ABI probes
are useful but do not certify HC16xx timing/audio/display or an Ingenic board.
New profiles stay experimental until physical evidence and redistribution rights
are reviewed. Do not ship vendor firmware merely because private adaptation works.
