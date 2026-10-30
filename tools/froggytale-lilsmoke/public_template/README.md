# Undertale-LilSmoke

Public release **0.0.1**. An experimental CPU-only Undertale runner for the
Linux/MIPS LilSmoke environment, adapted by **José Pilas** from FroggyTale and
Butterscotch. You must supply your legally obtained game files.

The release deliberately preserves the working engine binary. Its `--version`
and preparation screen still identify the internal engine as **0.1.0**. That is
not the public package version and does not indicate a different engine.
See [provenance](evidence/qualification.json) and [release notes](CHANGELOG.md).

## Install And Supply Your Game

1. Obtain the `.lilpck` from this repository's **0.0.1** release and install
   it using LilSmoke's application installer. Keep the corresponding-source ZIP
   alongside the package when redistributing it.
2. In the installed `Undertale-LilSmoke` app directory, create `original/`.
3. Copy your own Undertale **1.08 / GameMaker WAD16** `data.win` into that
   directory. `original/game.unx` or `original/game.ios` are alternatives.
   Place your own external `.ogg` music files beside the game file, preserving
   their original names. Other external game files must also come from your copy.
4. Launch **Undertale-LilSmoke** from the Applications menu. On first launch,
   the native converter displays stage/percentage/input/atlas progress and
   creates a private cache. Allow at least **128 MiB free space**, plus space
   for your original game/music and saves.

No EXE/DLL, game file, atlas, music, Flowey artwork, screenshot, firmware or
preparation cache is provided. Do not use the game-derived atlas offered by
the upstream handheld port. This is an independent, unofficial runner, not an
Undertale redistribution or a project endorsed by Toby Fox.

```text
Undertale-LilSmoke/
  original/data.win       # supplied by you
  original/*.ogg          # supplied by you
  cache/                  # generated locally; never redistribute
  saves/                  # your private saves
  run.sh
  runtime/
```

Preparation never changes or deletes `original/`. B/Escape cancels; partial
output is not a complete cache. Valid later launches reuse the cache. Changed
source data selects a new generation; damaged caches are retained with an
`.invalid` suffix and rebuilt. Back up saves before updates or uninstalling:
**uninstall can remove all files inside the installed app directory**.
Public 0.0.1 is not an automatic upgrade over private 0.1.0 installations.

## Controls And Saves

| Action | Controller | Keyboard |
| --- | --- | --- |
| Move | D-pad / left stick | Arrow keys |
| Confirm / interact | A | Z |
| Cancel | B | X |
| Menu | X / Y / Select | C |
| Start | Start | Enter |
| Exit runner | Start + Select | Close the window |

Preparation cancellation also accepts Escape. Saves default to `saves/`;
`FROGGY_SAVE_DIR` and `FROGGY_CACHE_DIR` can override writable locations.
The adapter supports evdev keys, SDL controllers, the MIT LilSmoke gamepad
transport and read-only stock shared input. It does not import UI custom
evdev keymaps. Load only trusted game files: the interpreter is not a sandbox.

## Platform And Limits

MIPS32r2 little-endian, o32, hard-float musl; 320x240 RGB565 software graphics
and incremental SDL2 audio. No GPU, bare-metal SF2000 firmware or Windows
execution path is used. This target's hard-float ABI is intentional; upstream
SF2000 soft-float build instructions describe a different device.

`run.sh` imposes a 112 MiB address limit. Generic SDL software presentation is
available; native M8 presentation expects display/audio libraries already
provided by the LilSmoke OS. Those OS libraries, kernel and musl are not in
this package and are not relicensed by this repository.

Video and Box2D are disabled. Atlas blocks are capped at 320 pixels with integer
box filtering, not pixel-identical to the optional Pillow-based upstream packer.
Retained evidence covers synthetic input/cache/failure cases and a bounded
owner-game opening/audio/FAT32-remount smoke test in a one-CPU 128 MiB MIPS VM.
It is not a full playthrough, general game compatibility, hardware performance,
speaker/HDMI/latency, or real save/load gameplay qualification. No full-speed
claim is made. No new owner-game or physical test is claimed for this repack.

## Offline Source Build

The checkout and same-release corresponding-source ZIP contain the pinned
asset-free upstream subset, adapters, staging transformations, MIT input
header, public-domain font, exact SDL2 build headers, SDL2 2.28.5 sources,
and GCC 12.3 libatomic sources with configure support. No network fetch is
required. Supply an existing compatible GCC 12.3 MIPS/musl SDK and Python 3.9+
(3.12 used for qualification):

```sh
export SDK=/path/to/buildroot/output
python3 -B build.py --out /path/to/new-output --verify-only
python3 -B build.py --out /path/to/another-new-output
```

Builds are serial, one allowed host CPU, nice 15, with a 768 MiB compiler address
limit. Set `FROGGY_CPU` to an allowed CPU if needed. The build does not change
the SDK. [Dependency instructions](docs/DEPENDENCIES.md) explain optional
out-of-tree SDL2/libatomic builds. General-purpose compiler, linker, C library,
kernel and host build tools remain recipient-supplied system prerequisites.

The exact source units, headers, staged audio and loop transformations are
verified against the qualified build's digests. A relocated source build can
have a different ELF hash because compiler paths/build IDs differ. The release
packager instead checks and preserves the qualified runtime hash byte-for-byte.
The private engine's compiled diagnostic source-location strings are not
rewritten. No claim of bit-for-bit rebuilt ELF reproducibility is made.

## Source And Licensing

This is **not a single-license MPL project**. The runner and derived adapters
remain **AGPL-3.0-only**. Existing AGPL tools retain that license. Only new,
independent `tools/audit.py` and `tools/release.py` use **MPL-2.0**.
Vendor code retains its own licenses. See [LICENSE](LICENSE), [NOTICE](NOTICE.md),
[license review](docs/LICENSE-REVIEW.md) and [third-party notices](THIRD-PARTY.md).
The package's SOURCE-OFFER points to the corresponding-source ZIP in the
**same 0.0.1 release**, not merely the unmodified upstream or mutable HEAD.

## Credits

- **José Pilas**: LilSmoke adaptation, preparation, input/audio integration and packaging.
- **Synaps33 and FroggyTale contributors**: the pinned handheld/software-renderer port.
- **mrpowergamerbr and Butterscotch contributors**: the GameMaker runner reimplementation.
- **Toby Fox and Undertale's credited creators**: the original proprietary game;
  their game/art/music are not included and no affiliation is implied.
- SDL contributors, the GCC/libatomic contributors, miniaudio/stb, bzip2,
  miniz, base64, MD5/SHA1 authors, Daniel Hepper/Marcel Sondaar/IBM font authors,
  and the MIT native input contributors: their notices remain in source.

Upstream: https://github.com/Synaps33/FroggyTale at
`5699418f671dc8bb07dc2e4adc60169b616ab906`, based on
https://github.com/ButterscotchRunner/Butterscotch.
The upstream README is retained for attribution/history; its device-specific
installation instructions and game-atlas download are not this port's setup.
