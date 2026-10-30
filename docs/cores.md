# LilSmokeOS Emulation Cores - Alpha-0.0.1

This guide describes the current system registry, installed-provider selection,
RetroArch integration and independent emulator Applications. It replaces the
historical J2 overview. A registered system or provider is not an inventory of
the Alpha image, proof that an engine is installed, or a full-speed/game
compatibility guarantee.

## Hardware And Scope

The tested SEGAM-M8-V6.0 lineage uses HC16xx/MIPS 74Kc, one Linux CPU and
128 MiB physical RAM, with roughly 110 MiB usable by Linux. Its stock kernel
is Linux 4.4.186; LilSmoke uses musl userspace. It is not an Ingenic JZ4770
board or a universal 256 MiB target. Display scaling is a 2D facility, not
OpenGL/GLES/Vulkan or a 3D GPU. Other board profiles need separate validation.
Host and Malta/QEMU results do not establish physical M8 speed, HDMI or sound.

## Registered Systems And Providers

The authoritative [system definitions](../package/lilsmoke-scripts/src/EmulatorSystems.h)
contain **24 canonical IDs**. The [provider registry](../package/lilsmoke-scripts/src/EmulatorRegistry.h)
maps them to the following candidates; missing binaries are marked unavailable.
Names in this table do not mean every candidate is bundled or qualified.

| ID | System / content | RetroArch candidates | Native OpenDingux / Application candidates |
|---|---|---|---|
| `nes` | NES / Famicom / FDS | FCEUmm, QuickNES | FCEUX (`fceu320`) |
| `snes` | Super Nintendo | Snes9x 2005, Snes9x 2002 | PocketSNES |
| `n64` | Nintendo 64 | ParaLLEl N64, software profile | No fixed native provider |
| `gb` | Game Boy | Gambatte | Gnuboy, Gambatte |
| `gbc` | Game Boy Color | Gambatte | Gnuboy, Gambatte |
| `gba` | Game Boy Advance | mGBA, gpSP | ReGBA, native mGBA |
| `md` | Mega Drive | Genesis Plus GX, PicoDrive | PicoDrive |
| `sms` | Master System | Genesis Plus GX, PicoDrive | PicoDrive |
| `gg` | Game Gear | Genesis Plus GX, PicoDrive | PicoDrive |
| `pce` | PC Engine | Mednafen PCE Fast | Temper |
| `arcade` | Arcade ROM sets | FinalBurn Neo | MAME4ALL (0.37b5), FBA SDL |
| `lynx` | Atari Lynx | Handy | Handy |
| `psx` | PlayStation | PCSX ReARMed | PCSX4ALL Application |
| `atari2600` | Atari 2600 | Stella 2014 | Stella |
| `atari7800` | Atari 7800 | ProSystem | ProSystem |
| `atari800` | Atari 800 / 5200 | Atari800 | Atari800 |
| `ngpc` | Neo Geo Pocket | RACE | RACE |
| `wswan` | WonderSwan | Beetle WonderSwan | Oswan |
| `pokemini` | Pokemon Mini | PokeMini | PokeMini |
| `doom` | Doom IWAD / project | PrBoom | PrBoom |
| `cavestory` | Cave Story data | NXEngine | NXEngine |
| `easyrpg` | RPG Maker 2000 / 2003 project | EasyRPG | EasyRPG |
| `scummvm` | ScummVM marker and game data | ScummVM (2D) | ScummVM (2D) |
| `openbor` | OpenBOR PAK | No fixed libretro provider | OpenBOR (legacy 32-bit) |

Installed Applications can add providers through `open_core`. Expansion-system
rows are filtered by installed providers: normally both RetroArch and native
providers are needed, or an emulator Application; OpenBOR also appears with
its native provider alone. Base-system rows can remain visible without an
installed engine or ROM. Thus neither the Settings row count nor the source
registry is a shipping or gameplay-qualification count.

Classic PC engines require complete compatible data: `Doukutsu.exe` plus Cave
Story data, `RPG_RT.ldb` plus its project, a `.scummvm`/`.svm` marker plus game
files, the appropriate Doom IWAD, or an OpenBOR `.pak`. A marker EXE is not
executed as a Windows program. BIOS, ROM sets and game assets remain the user's
responsibility; arcade set versions and required BIOS differ by provider.

## Select A Provider

In **Settings > Emulator Core Settings**:

- Left/right switches the global RetroArch/OpenDingux policy and clears
  per-system exceptions.
- A or Start opens the per-system list. Select a system, then an installed
  provider or **Follow global setting**. This does not change the global policy.
- An unavailable explicit selection produces a launch error, not a silent
  switch to another engine. A provider's presence is not a compatibility test.

Configuration uses `EMULATOR_BACKEND=retroarch|opendingux` and exceptions such
as `EMULATOR_snes=od:pocketsnes`, `EMULATOR_gba=ra:mgba` or
`EMULATOR_psx=app:org.lilsmoke.pcsx4all`. Empty exceptions follow global policy.
Initial migration preserves a legacy PocketSNES choice and selects the
PCSX4ALL app for PSX; old `od:pcsx4all` values migrate to its app ID. The
registry also prefers an installed PCSX4ALL app for PSX under an unset or
OpenDingux global policy, and allows native OpenBOR without inventing a
libretro provider. Other missing global-policy providers fail visibly.

The **OpenDingux** home entry opens each native engine's own menu, not the old
LilSmoke compatible-ROM selector. Selecting a ROM in a system list instead
routes that content through the configured provider. PCSX4ALL's menu lives in
**LilSmoke Applications**. See [OpenDingux integration](opendingux-integration.md).

## RetroArch Runtime

[lilsmoke-retroarch](../package/lilsmoke-scripts/src/lilsmoke-retroarch)
launches RetroArch 1.19.1 with software SDL2/DirectFB and a generated runtime
configuration. `/etc/retroarch.cfg` is the packaged baseline; persistent
preferences are `/persist/config/retroarch.cfg` and
`/persist/config/retroarch-core-options.cfg`. Hardware constraints precede
preferences and baseline defaults; duplicate keys keep their first definition.

| Area | Current behavior |
|---|---|
| Video | `sdl2`, software rendering; M8 native framebuffer adapter/presentation, no GPU APIs |
| Audio | Generic profile: ALSA. M8 HCFB profile: `lilsmoke` native PCM driver, not universal ALSA; SDL audio is set to dummy there |
| Input | SDL2 event input; generic joypad backend `linuxraw` or `sdl2`; M8 uses the `lilsmoke` joypad driver and configured shared input |
| Menu | RGUI; M8-specific presentation/aspect settings override generic fullscreen/integer defaults |
| Saves / states | `/persist/saves` and `/persist/states`, overridable by launch environment; states sorted by core/content |
| BIOS | RetroArch system directory `/storage/bios`; names and requirements depend on the core |
| Costly features | Rewind, run-ahead, shaders and filters disabled by default; online core updater, networking and achievements disabled in this build |

System cores are discovered in `/usr/lib/libretro`; optional cores in
`/storage/emulators/libretro`. Matching info files use
`/usr/share/libretro/info` and `/storage/emulators/info`. The launcher merges
installed locations in RAM for the RetroArch menu, with OS files taking
precedence when filenames collide. Optional binaries still need the matching
MIPS/musl ABI, runtime dependencies and software-only interfaces.

### Save And Load State

The launcher enables **Save State** and **Load State** Quick Menu controls,
but an engine must actually implement compatible serialization and have the
correct core info metadata. An absent/disabled action does not by itself mean
the UI or storage is broken. SRAM, PSX memory cards and in-game saves are
separate from emulator states; states are not portable between unrelated
providers or necessarily between versions.

Pinned core info files are renamed to match installed `.so` names and metadata
caching is disabled. Snes9x 2005 exposes **basic**, not deterministic, states:
video restoration was tested, but the legacy serializer omits audio history
and sample phase. Other cores do not inherit that test coverage. Optional
RetroArch clean-exit auto-resume also depends on the selected core's state
support. See [core metadata](../package/retroarch-mips/core-info/README.md).

## PlayStation: PCSX4ALL And PCSX ReARMed

PCSX4ALL is an independent preinstalled Application with ID
`org.lilsmoke.pcsx4all` and `open_core=psx`; select
`app:org.lilsmoke.pcsx4all` in Settings. Its own menu opens without content;
system-list launches pass the selected PSX game through the Applications
helper and shared resolver. Boot seeding preserves renamed apps, user data
and intentional uninstall rather than reinstalling at every boot.

RetroArch **PCSX ReARMed** (`ra:pcsx_rearmed`) is a separate optional provider
when its core is installed. PSX is therefore not categorically "non-libretro".
Owner feedback favors PCSX4ALL on the tested M8; ReARMed has performance
concerns, and neither choice guarantees every game's accuracy, sound or speed.
The M8 ReARMed launch profile disables GPU/SPU worker threads and NEON
enhancements. Generic registered N64 software support likewise does not imply
usable speed or GPU acceleration. PSP, Dreamcast and PS2 are not registered.

### PCSX4ALL Build And Storage

The [Buildroot package](../package/pcsx4all/pcsx4all.mk) links the reconstructed
MIPS dynarec core to the C++ SDL2 frontend, native menu and shared Input code.
It uses a hash-checked qualified core archive if the configured local artifact
exists; otherwise it rebuilds `Makefile.rkgame` with the target toolchain.
The frontend is linked non-PIE at `0x40000000` for the engine's memory layout.
This replaces the obsolete J2 description of an always-prebuilt SDL1 library
and tiny C-only launcher. Neither source provenance nor a dynarec is a speed
guarantee.

The package builds a `.lilpck` and installs it under
`/usr/share/lilsmoke/builtin-apps`; `S08applications` seeds it into
`/storage/independent-apps`. The OS app metadata uses internal version 1.0.6;
the separate public PCSX4ALL-LilSmoke 0.0.1 artifact has its own evidence and
must not be confused with that installed build.

| Default path | Purpose |
|---|---|
| `/persist/config/pcsx4all/pcsx4all.cfg` | Persistent native configuration, initialized from app defaults |
| `/persist/saves/pcsx4all/mcd001.mcr`, `mcd002.mcr` | Default memory cards, not one invented card filename per game |
| `/persist/states/pcsx4all/<content-key>/slot-0.state` through `slot-9.state` | Ten frontend state slots per logical content identity |
| `/persist/bios/scph1001.bin` | User-supplied 524,288-byte BIOS recognized by the default setup |

Without that valid BIOS the engine defaults to HLE; HLE is not equivalent to
full BIOS/game compatibility. Existing user configuration can override defaults.
No BIOS is bundled. The native pause menu exposes slot selection, save/load,
resume and exit independently of RetroArch's Quick Menu. Start+X opens that
menu; Start+Select exits. `/etc/pcsx4all.cfg` is a default, not the live user
configuration. Its setting syntax is `FrameSkip 0`, not `frame_skip = 1`.

## ZIP, 7z And Multi-File Content

[lilsmoke-rom](../package/lilsmoke-scripts/src/lilsmoke-rom.cpp) is the shared
storage-backed resolver for launchers and adapted in-engine browsers, including
the RetroArch content-load hook. Recognized wrappers include ZIP, 7z, RAR,
TAR and supported gzip/bzip2/xz variants. Libarchive support, decoder memory
and the destination filesystem still limit what can be opened.

Non-arcade archives extract to
`/storage/.cache/lilsmoke/archives/<system>/<archive-sha256>/`; completed caches
are reused until archive contents change. The resolver keeps companion files
but returns one primary file by system extension priority and sorted path.
It is not a multi-game selection dialog: prefer one game/project per archive.
Arcade ZIP sets pass through unchanged; other arcade containers, including
7z, are extracted and normalized to an uncompressed ZIP for the engine.
This does not convert one emulator's ROM-set version into another's.

The helper rejects traversal, links and special files, bounds extraction to
20,000 entries / 2 GiB expanded data, and requests a 96 MiB address-space
ceiling. These are limits, not a promise that every archive fits the M8.
Extraction and arcade normalization need free storage; use modest compression
dictionaries and preserve originals.

CUE files need every referenced track; M3U disc lists need their referenced
discs, and data projects need all companion assets. Preserve relative names
and folder structure. The PCSX4ALL app prefers a sibling `.cue` when passed a
`.bin`/`.img`. The scanner's PSX extensions, including CHD/M3U, are not proof
that every PSX provider can decode or switch those discs. In particular, the
archive resolver's PSX primary-file list does not select M3U; use an intact
uncompressed playlist/set when the chosen engine supports it. The resolver
does not repair missing tracks or validate a complete multi-disc workflow.

## Launch And Build Integration

LilSmokeUI hands content to `/usr/lib/lilsmoke/lilsmoke-launch SYSTEM ROM`.
The registry resolves global/per-system policy and executes the selected
RetroArch, native or Application provider. Emulator Applications receive the
original system-relative logical filename and resolve archives in their own
launcher; non-app routes resolve before engine handoff. Launches are blocked
while LILDRIVE is exported. Metadata is not shell-evaluated.

```sh
/usr/lib/lilsmoke/lilsmoke-launch snes '/storage/roms/snes/Game.7z'
/usr/lib/lilsmoke/lilsmoke-launch psx '/storage/roms/psx/Game.cue'
/usr/lib/lilsmoke/lilsmoke-launch arcade '/storage/roms/arcade/set.zip'
```

The base `retroarch-mips` recipe builds five curated cores (FCEUmm, Gambatte,
mGBA, Snes9x 2005, Genesis Plus GX), adding FBNeo, Mednafen PCE Fast and Handy
outside `BR2_PACKAGE_LILSMOKE_LITE`. Those eight build choices are not the
24-system registry or the optional-provider inventory. Native/expansion
builds and separately installed Applications are additional delivery paths.

To add a core, pin its six-field source/build/output entry in
`package/retroarch-mips/cores.lock`, include it in `LILSMOKE_CORES`, and vendor
matching info metadata with provenance. Update registry/routing and shared
system/content definitions as needed, then use the matching Buildroot target
toolchain. The helper `build-cores.sh` builds pinned sources and records build
info; merely naming a core does not install it. Validate ELF/ABI, API loading,
real content, input, audio, states and whole-device memory before delivery.

Historical J2 "full speed", fixed thirteen-system, stock-SDL1 and Ingenic
assumptions are not Alpha acceptance evidence. Preserve working providers and
saves when testing alternatives. See [Applications](lilsmoke-applications.md)
and [performance evidence and limits](performance.md); older VM timings are
not physical M8 benchmarks.
