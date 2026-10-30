# SM64-LilSmoke

Native original-engine Super Mario 64 adaptation
for LilSmokeOS on the tested MIPS device lineage.

This repository contains source and build/extraction tools, not a ROM,
extracted texture/music/sample collection or a pre-cooked full game.
The original game engine is based on [n64decomp/sm64](https://github.com/n64decomp/sm64)
and the pinned [DrUm78 software port](https://github.com/DrUm78/sm64-port).
It is not a recreated-game demo or a Windows emulator.

## Download

[Pre-Alpha-0.0.2](https://github.com/josepilas/SM64-LilSmoke/releases/tag/Pre-Alpha-0.0.2)
provides an approximately **44 MiB** asset-free `.lilpck`, a separate `corresponding-source.zip`,
and SHA-256 sidecars. Only the `.lilpck` is needed to install and play. Its root
`SOURCE-OFFER.txt` links to the matching dependency sources and project snapshot.
The offline preparation/runtime files and license notices stay in the app. The source
is public here; authored adaptations use MPL-2.0.

This release promotes the verified local preparation/cached-launch fixes. It
does not include a precompiled original game or remove the initial local build.
See [CHANGELOG.md](CHANGELOG.md) for changes and the exact testing limits.

## Changes In 0.0.2

- Preparation works with a 1970/no-RTC clock and FAT32 source timestamps,
  without changing the device's clock or disabling dependency checks.
- Locally generated sound data uses assembly `.incbin` instead of large C
  initializers. Compiler diagnostic-memory overhead and duplicate dependency
  compilation are reduced without lowering gameplay/renderer optimizations.
- Interrupted preparation resumes verified work, including recovery from a
  power cut during the final executable-copy transaction.
- Progress names the current extraction/build task; the extracted-MB counter
  is separate from compilation progress. B cancels without deleting the ROM.
- A valid prepared cache launches directly, without starting the preparation
  worker/UI again. The game has a separate 96 MiB virtual-address guard, which
  includes file/MMIO mappings; it is not a 96 MiB physical-RAM allocation.
- Source distribution includes the new preparation helpers and matching SDL
  changes. Dependency sources remain separate from the installation package.

## Bring Your Own Data

Install the `.lilpck` through LilSmoke Explorer, then put one supported **US
1.0** ROM in the installed application's `original/` directory. Its name is
arbitrary; Z64, V64 and N64 byte order are identified from contents.
Expected normalized SHA-1: `9bef1128717f958171a4afac3ed78ee2bb4e86ce`.

First launch extracts data and prepares a private native runtime. The loading
screen displays the phase, percentage and MB extracted. Initial preparation
also compiles source locally and can take substantial time; later launches
use the verified cache. Cancel leaves the original ROM intact. After a
durable verified cache and original-engine preflight succeed, only the copy
inside the application's `original/` is deleted. Keep your separate dump backup.

Do not distribute the generated `.sm64-cache` or its game executable. The
upstream graphics implementation permits source redistribution but explicitly
prohibits binary redistribution; its binary is compiled locally, not shipped.
Save data is kept separately in `saves/` and survives a cache rebuild.

For US 1.0, extracted data totals about **10.68 MB**. That number stops increasing
when extraction finishes; texture conversion and compilation still have work to
do. Follow the displayed task/percentage rather than treating the MB counter as
a compilation counter. Initial preparation can still take a long time on this
CPU. Do not unplug power while it is preparing or saving.

When updating through LilSmoke Explorer, keep the app's name/ID and use a recent
LilSmokeOS installer that preserves a validated prepared cache and `saves/`.
Keep a separate backup of saves. An invalid or incompatible cache is rebuilt;
do not copy a cooked game from another user or distribute your private cache.

## Controls

| Input | Action |
| --- | --- |
| Left stick | Movement and speed |
| A | Jump |
| X / B | Punch, kick, grab and throw |
| LT | Crouch, long jump and ground pound |
| RB | Camera mode |
| Right stick | N64 C-button camera controls |
| Start | Original pause |
| Start + X | LilSmoke options menu |
| Start + Select | Exit |

The LilSmoke options menu contains the existing quality/scaling and volume
controls. Higher world resolution costs CPU time; this update does not promise
full-speed high-resolution rendering on every scene/device.

## Target And Build

The supported runtime is MIPS32r2/o32 little-endian, musl, software SDL2/DirectFB
and CPU-only rendering. No OpenGL, GLES, Vulkan or desktop display server is
required. The tested reference device has one MIPS CPU and 128 MiB RAM; this
is not a claim that every M8 marketing variant is compatible.

The preparation UI and authored input objects can be rebuilt without any ROM:

```sh
make SDK=/path/to/lilsmoke-sdk
```

Set `CROSS` and `SYSROOT` when using a differently laid-out SDK. The SDK needs
SDL2, libwebp, json-c and the C++17 musl toolchain. Runtime cooking uses bundled
native tools and matching source materials. Source/build recipes are retained
under `tools/`; original engine and platform changes retain their own notices.

Run transaction/package regression tests on Linux with `make test`. To rebuild
the asset-free package, unpack the release and use its `runtime/` as the reviewed
compiler/library template:

```sh
python3 tools/package.py --runtime /path/to/unpacked/runtime \
  --sdk /path/to/lilsmoke-sdk --output SM64-LilSmoke-Pre-Alpha-0.0.2.lilpck
```

This rebuild compiles the authored UI/input code and conversion tools from source.
It does not need a ROM, cook game data, or distribute a graphics binary. The
native compiler, Python and dependency rebuilding recipes and matching source
materials are supplied in the separate `corresponding-source.zip` release asset,
not copied onto the console. Extract that ZIP alongside the unpacked application
when inspecting or rebuilding dependencies; it is not required for playing.
The runtime supplies musl libraries; DirectFB and hardware display/audio drivers
are part of the LilSmokeOS platform contract.

When publishing modified binaries, regenerate the matching source archive and
its download pointer rather than reusing the original release's source offer.

## License

Authored LilSmoke adaptations are **MPL-2.0**. Upstream game-engine source,
graphics code, fonts, tools, compiler and libraries retain their individual
licenses. `LICENSE` is not a blanket grant for Nintendo's game assets or for
redistribution of the locally generated original-game runtime.

Release acceptance must include fresh-ROM extraction, controller cancellation,
original-game launch, restart without the ROM, saves and the target RAM budget.
See `RELEASE-STATUS.json` for the exact completed tests and outstanding limits.

The completed qualification combines a full native-MIPS extraction/build with
separate constrained whole-system resume/game and cached-launch tests (one CPU,
120 MiB physical RAM, no swap, FAT32/no-RTC preparation). It is not a full fresh
compilation measured on the physical HC16xx, nor a hardware FPS/audio guarantee.
