# Pre-Alpha-0.0.2

The current asset-free SM64-LilSmoke runtime for LilSmokeOS, including the local
preparation and repeat-launch fixes developed since Pre-Alpha-0.0.1.

## Downloads

- **Install/play:** `SM64-LilSmoke-Pre-Alpha-0.0.2.lilpck` (about 44 MiB).
- **Sources/developers:** `corresponding-source.zip`, separately downloadable.
- SHA-256 sidecars are provided for both. The app's `SOURCE-OFFER.txt` links to
  this release's matching source archive and checksum.

## Changes

- Fixed FAT32/no-RTC GNU Make restart loops without changing the system clock.
- Reduced preparation memory with local audio `.incbin`, single-pass dependency
  compilation and reduced diagnostic macro tracking. Renderer optimization and
  gameplay code are not downgraded.
- Improved cancellation, verified resume and interrupted final-copy recovery.
- Added task/phase-based preparation progress and elapsed time; extracted MB
  remains an extraction counter, not a compiler counter.
- Valid prepared games launch directly without repeating preparation/UI work.
- Updated controller/input and SDL source/build materials in the public tree.
- Preserved the verified build/cache recipe across this release's metadata update.

## Bring Your Own Data

Install through LilSmoke Explorer. Put one **US 1.0** ROM in the application's
`original/` folder, using any filename. Z64, V64 and N64 byte ordering are
detected from contents. First launch extracts assets and builds locally;
it can still take a long time. Only the application's ROM copy is deleted,
and only after a durable verified game cache and preflight succeed.
Keep your own ROM/save backups. No ROM, extracted game assets, cooked original
game or restricted graphics binary is included in these downloads.

Use a recent LilSmokeOS installer for prepared-cache-preserving updates.
An incompatible or damaged cache must be rebuilt. Start+X opens LilSmoke
options; Start+Select exits. See the updated README and CHANGELOG for controls.

## Testing And Limits

The operational runtime is the verified local prep.8 payload. Its qualification
combines full native-MIPS preparation, separate FAT32/1970 constrained resume
tests, installation/read-back and cached launch/menu/relaunch with one CPU,
120 MiB physical RAM and no swap. Release packaging changes only version/source
delivery metadata, not executable/library/engine/cache-recipe bytes.

This is pre-alpha software: not a full fresh compilation timed on physical
HC16xx, cycle-accurate performance, a hardware FPS promise or audible-output
certification. Higher world resolution costs CPU time.

Authored adaptations: Jose Pilas, MPL-2.0. Thanks to n64decomp/sm64, the retained
DrUm78 software-port authors, and the compiler/library/tool contributors credited
in the repository and dependency notices. Every upstream license remains in force.
