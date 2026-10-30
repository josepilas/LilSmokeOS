# SM64-LilSmoke Pre-Alpha-0.0.1

By Jose Pilas. Native original-engine Super Mario 64 for LilSmokeOS's tested
HCSEMI MIPS lineage. This is not the earlier reconstructed-game prototype.

## Installation

Download only the `.lilpck` to install and play (43.6 MiB, down from 311.9 MiB).
`corresponding-source.zip` is a separate optional download for developers and
source inspection. `SOURCE-OFFER.txt` inside the app links directly to that ZIP
and supplies its SHA-256. No additional download is needed on the console.

1. Download `SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck` and put it on LILROMS.
2. Open it in LilSmoke Explorer and select INSTALL.
3. Copy one supported US 1.0 ROM into the installed app's `original/` directory.
   Any filename is accepted. Z64, V64 and N64 byte orders are detected by contents.
4. Launch Super Mario 64 in LilSmoke Applications. First launch extracts assets
   and compiles the original engine locally; allow substantial preparation time.
   The screen shows progress, percentage and actual MB extracted. B cancels.
5. After a durable cache commit and original-engine render check, only the ROM
   copy in this app's `original/` is deleted. Keep a separate dump backup.

Supported normalized US 1.0 SHA-1:
`9bef1128717f958171a4afac3ed78ee2bb4e86ce`.

No ROM, extracted Nintendo texture/music/sample collection, cooked original-game
executable, or restricted graphics binary is included. The graphics source's
binary-redistribution prohibition is retained: users compile it locally.
Do not redistribute `.sm64-cache` or the locally generated game.

## Controls

Left stick moves; A jumps; X/B attacks; LT crouches; RB changes camera;
right stick controls the C-button camera; Start pauses. Start+X opens options;
Start+Select exits. Saves and quality/scale/volume options persist in `saves/`.
The extraction screen uses a native 128x128 WebP icon.

## Tests And Limits

- 27 transaction, source-safety, distribution-manifest and source-split tests passed on Linux
  and with the native MIPS Python runtime.
- A fresh owner ROM extracted 10,679,837 bytes, linked the original game,
  passed its 30-frame render preflight, committed the cache and deleted only
  the app copy. Restart without the ROM used the verified cache.
- Production LilSmokeUI -> Applications -> preparation bootstrap -> original
  game -> options/resume -> Applications passed in a single-CPU 120 MiB MIPS
  Linux VM, with a 64 MiB game address-space limit and 3,360 fully rendered frames.
  Original audio synthesis produced nonzero samples. This does not prove
  audible output on the physical M8; the VM lacks its auddec hardware.
- The native renderer compiler passed a separate full-VM test under a 90 MiB
  process address-space limit; observed peak virtual memory was 85,800 KiB.
- Malta/QEMU is not the HC16xx SoC. No physical FPS claim, universal M8 support,
  or complete concurrent first-run RAM/timing certification is made. Physical
  acceptance of this new public cooker is still needed.

The package includes bundled native compiler/runtime dependencies, their notices,
operational engine source and a reviewed checksum manifest. Distribution-only
dependency source archives, build inputs and the public project source snapshot
are provided as `corresponding-source.zip` in this same release. The smaller
package passed the production MIPS installer and 3,135 installed-file checks.
Its operational payload and preparation recipe are unchanged. Initial preparation
needs free SD space for its temporary source/build tree in addition to the package.
Authored adaptations are MPL-2.0; all upstream parts retain separate licenses.
