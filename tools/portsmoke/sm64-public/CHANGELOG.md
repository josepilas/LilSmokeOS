# Changelog

## Pre-Alpha-0.0.2 - 2026-10-23

Promotes the verified local prep.8 runtime and the earlier preparation fixes.
The original-engine source, gameplay/renderer optimization flags and private
game-cache recipe are unchanged by release packaging.

### Fixed

- GNU Make restarting until timeout on no-RTC/1970 systems with FAT32 inputs.
  Successful build outputs use a bounded logical timestamp; the system clock
  and ordinary dependency validation are unchanged.
- Excessive compiler memory while parsing locally generated audio byte arrays.
  Assembly `.incbin` preserves the exact generated sound bytes/symbol layout.
- Duplicate C dependency compilation and macro diagnostic-history memory use.
  This does not replace optimized rendering with a lower-quality build.
- Interrupted final executable-copy recovery before a durable cache commit.
  Unsafe linked outputs fail closed; the original ROM survives failed work.
- Prepared-game launches unnecessarily starting preparation and its UI again.
- SDL/native-MMIO virtual-address pressure at game launch. The separate game
  guard includes mappings and is not a physical-RAM reservation.

### Improved

- Phase/task-based preparation percentage, elapsed time, extracted MB counter,
  cancellation, verified resume and repeat launch without the ROM.
- Current authored controller/input sources and helpers in the public build.
- Matching SDL changes/configuration and the complete new preparation sources
  in the separate corresponding-source download.

### Installation And Data

Download only `SM64-LilSmoke-Pre-Alpha-0.0.2.lilpck` to install through LilSmoke
Explorer. `corresponding-source.zip` is a separate developer/source-compliance
asset, not an installation dependency. `SOURCE-OFFER.txt` inside the app points
to this release's exact source archive and checksum.

Put one supported US 1.0 ROM, with any filename, in the installed app's
`original/`. Z64/V64/N64 byte orders are content-identified. Only the app's ROM
copy is deleted, and only after a durable verified build and original-engine
preflight. No ROM, extracted game assets or cooked original-game executable is
distributed. Keep your own dump and save backups.

The 10.68 MB asset counter stops increasing during compilation. First-time
preparation can still be lengthy. A validated compatible ready cache is kept
by the recent LilSmokeOS installer; incompatible caches must be rebuilt.

### Qualification Limits

The local predecessor passed a full native-MIPS preparation, separate FAT32 /
1970 constrained resume-and-game tests, production installation/read-back,
and cached launch/menu/relaunch with a one-CPU, 120 MiB, no-swap MIPS VM.
Release metadata/source-offer changes do not alter operational runtime bytes.
The new archive is checked separately before publication.

This is split functional qualification, not a complete fresh compilation timed
on a physical HC16xx, cycle-accurate emulation, a hardware FPS guarantee or an
audible-output certification. First preparation still compiles upstream source
locally. Authored parts use MPL-2.0; every dependency retains its own terms.

## Pre-Alpha-0.0.1

Initial public, asset-free BYO-data release with local original-engine building,
WebP extraction UI, transactional cache and separate corresponding source.
