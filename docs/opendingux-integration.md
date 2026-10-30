# OpenDingux Integration - Alpha-0.0.1

This guide describes the current native-engine, OPK and emulator-provider
integration. "OpenDingux" is a source-port/menu channel here, not a claim that
LilSmoke runs the original OpenDingux OS or every legacy binary package.
The old Candidate11 deployment record discussed below is historical, not
Alpha inventory or installation instructions.

## Navigation And Provider Policy

- The **OpenDingux** home entry lists installed native engines and imported
  OpenDingux packages. Selecting a native engine opens its **engine-owned
  menu** through `lilsmoke-emulators menu ID`, not the old LilSmoke ROM selector.
  Imported packages use their supported menu entry; incompatible imported
  packages remain visible with a diagnostic. Missing built-in engines are hidden.
- Selecting a ROM from a console/system list is a different operation:
  `lilsmoke-launch SYSTEM ROM` routes to that system's configured provider and
  passes content directly. Opening an engine menu does not change that policy.
- In **Settings > Emulator Core Settings**, left/right switches the global
  backend between RetroArch and OpenDingux and clears per-system exceptions.
  A or Start opens the per-system list; select a system and choose an installed
  provider or **Follow global setting**. An exception can use a different
  backend or an independent emulator Application.
- The current registry defines **24 system IDs**, not thirteen installed or
  fully compatible emulators. Base rows can appear without ROMs or engines.
  Expansion rows normally require both installed backend candidates or an
  emulator Application; native OpenBOR is also admitted on its own. Choices
  are filtered by installed available providers. See the complete
  [system/provider table](cores.md#registered-systems-and-providers).
- Missing/stale explicit providers fail visibly instead of silently falling
  back. Global policy normally selects the first installed matching provider.
  Its explicit exceptions are installed PCSX4ALL for PSX under unset/OpenDingux
  policy and native OpenBOR when no global-policy provider exists.

Configuration stores `EMULATOR_BACKEND=retroarch|opendingux` and optional
`EMULATOR_<system>` values such as `od:pocketsnes`, `ra:mgba`, or
`app:org.lilsmoke.pcsx4all`. Empty values follow global policy. First migration
preserves legacy PocketSNES and PCSX4ALL selections as exceptions under the
RetroArch default; `od:pcsx4all` migrates to the Application ID. Settings and
execution share [EmulatorRegistry.h](../package/lilsmoke-scripts/src/EmulatorRegistry.h);
metadata is never evaluated as shell commands.

## Independent Applications And PlayStation

PCSX4ALL now belongs in **LilSmoke Applications**, not the OpenDingux home
channel. Its package declares `open_core=psx`, so Settings exposes
**LilSmoke App: PCSX4ALL** with selection ID `app:org.lilsmoke.pcsx4all`.
A menu launch receives no emulator arguments; ROM selection uses the
Applications helper's `run-core` contract. Stable app IDs preserve selection
when an app is renamed. Bundled-app seeding preserves user data, does not
downgrade a newer user package and honors intentional uninstall.

Optional RetroArch PCSX ReARMed remains a separate PSX choice when installed.
Owner feedback favors PCSX4ALL on the tested M8 and identifies ReARMed speed
concerns; neither is a full-speed/all-games promise. Selecting the PSX system
does not force every launch to PCSX4ALL. Independent app runtimes and
`open_core` declarations also do not create emulation capabilities the engine
lacks. See [Applications](lilsmoke-applications.md) and
[PCSX4ALL details](cores.md#playstation-pcsx4all-and-pcsx-rearmed).

The direct **TreeFrog Ports** catalogue is a separate compatibility component,
not this native musl OPK loader or an Applications installation requirement.
Converted TreeFrog `.lilpck` packages instead use the Applications route and
their explicit compatibility adapter. See [direct ports and converter](treefrog-port-converter.md).

## Hardware And Runtime Boundary

The tested SEGAM-M8-V6.0 lineage is HC16xx/MIPS 74Kc, one Linux CPU, 128 MiB
physical RAM and roughly **110 MiB usable RAM**, Linux 4.4.186 and musl
userspace. It is not an Ingenic JZ4770 board. No 3D GPU/GL/GLES interface is
assumed; 2D framebuffer scaling is not 3D acceleration. Other boards require
separate qualification, and a 128 MiB VM is not a model of M8 cycle timing.

Original OpenDingux packages often expect an Ingenic display/input stack,
uClibc and loop-mounted OPKs. The tested M8 kernel has no loop/FUSE support
and `flock` is unavailable. Native MIPS/musl source rebuilds using the supported
software SDL layer are the intended path. Never rename/symlink a musl loader
to impersonate a uClibc/glibc loader.

The M8 also lacks an ALSA device. Adapted source engines use the native PCM
worker for sound; `SDL_AUDIODRIVER=dummy` in that profile does not mean the
native PCM path is muted. Generic/non-M8 profiles can use ALSA. Imported OPKs
do not receive an automatic legacy audio/ioctl translator: passing ELF and
package checks is not proof of sound, controls, speed or all application APIs.

The trusted native [launcher](../package/lilsmoke-scripts/src/lilsmoke-od-launch)
sets per-engine HOME under `/persist/od-engines/<engine>`, ROM roots under
`/storage/roms/<system>` and native display/input/audio flags. It replaces
arbitrary inherited preloads/search paths with the fixed OS adapter policy,
retaining only the validated loading gate when active. ScummVM uses its own
game cursor. Each engine still owns its menus, configuration and save formats.

**Save State / Load State are engine capabilities**, not universal OpenDingux
services. A native menu must implement them; RetroArch Quick Menu visibility
also depends on core serialization and matching `.info` metadata. PCSX4ALL
has its own ten-slot state menu. Battery saves/memory cards are distinct from
states, and switching providers does not make their states interchangeable.
See [state limits](cores.md#save-and-load-state).

## Shared Archive Resolution

ROM launches use the shared bounded `lilsmoke-rom` resolver, including ZIP/7z
and other supported wrappers, before handing resolved content to native
engines. Adapted native browsers use the same resolver rather than a second
archive decoder. Emulator Applications receive the original logical filename
and invoke resolution in their own launcher. Launch is blocked while
LILDRIVE is exported.

The resolver extracts to storage, not a whole-ROM RAM buffer, and caches by
archive SHA-256 under `/storage/.cache/lilsmoke/archives/<system>/`. Arcade
ZIPs pass through; arcade 7z/other supported containers normalize to ZIP.
Matching ROM-set versions are still required. Non-arcade archives retain
companion files but select one primary file by extension priority/sorted path,
not a multi-game chooser. Current limits are 20,000 entries / 2 GiB expanded
and a requested 96 MiB helper address-space ceiling, not guaranteed M8 capacity.

Keep every CUE track, M3U-referenced disc and project asset with its original
relative name. Archive acceptance and scanner extensions do not establish a
provider's CHD/M3U, disc-switching or multi-file game support. The PSX archive
primary-file list does not select M3U. No missing tracks or project files are
invented. See [archive/disc caveats](cores.md#zip-7z-and-multi-file-content).

## Native OPK And Desktop Import

Put native-compatible `.opk`/`.desktop` entries in `LILROMS/opendingux/apps`
(`/storage/opendingux/apps`). The second catalogue root is
`/usr/share/lilsmoke/opendingux/apps`; extraction cache is
`/storage/opendingux/cache`, with per-ID HOME at `/persist/opendingux/home/ID`.
The cache must be writable and executable. ROMs and proprietary assets are
not supplied by this layer. Executing native code is not a security sandbox.

The loader uses upstream libini metadata parsing and private libsquashfs
streaming, not mounts, FUSE or whole-package buffers. It checks every embedded
ELF against its native little-endian MIPS O32/FP32/musl subset and allowlisted
dependencies. Legacy glibc/uClibc loaders, GPU/private dependencies, unsafe
paths, links and special files are rejected. These musl OPK rules are separate
from TreeFrog's explicit device-matched legacy runtime.

`lilsmoke-opendingux scan` emits five bounded TSV fields:
`id`, `name`, `systemsCSV`, `reason`, `needsRom`. An empty reason means the
entry passes static policy, not gameplay qualification. `run ID [ROM]`
executes parsed argv without shell evaluation; `menu ID` uses the supported
menu entry. Package/cache budgets, desktop field-code rules and exact commands
remain in [runtime documentation](../tools/opendingux/README.md) and
[integration contract](../tools/opendingux/INTEGRATION.md).

## Build Integration

- `BR2_PACKAGE_LILSMOKE_OPENDINGUX` builds the OPK helper and selects its
  libsquashfs/zlib/xz/libsodium closure. The private SquashFS library is pinned
  to squashfs-tools-ng 1.2.0; libini has its own pinned source and LGPL terms.
- `package/opendingux-emulators` builds the pinned PocketSNES source port
  against the software SDL1.2 compatibility layer and installs it in
  `/usr/lib/lilsmoke/od-emulators`. Its patched browser, native PCM, evdev
  controls and framebuffer/menu restoration are not generic legacy-OPK support.
- `package/lilsmoke-od-engines` and `tools/emulators/od-engines/build.sh`
  produce additional native/Libretro expansion payloads with audited libraries,
  support data and notices. They stage under `emulator-expansion` in the build
  images directory, not an automatic install of every registered engine.
- Runtime lookup also supports `/storage/emulators/opendingux` and
  `/storage/emulators/libretro`. Use matching source/toolchain/runtime profiles;
  copying an arbitrary executable there does not adapt its ABI or hardware APIs.

Upstream engines/libraries retain their own licenses. Static registration,
successful source compilation and a public OS source tree are not blanket
binary redistribution clearance or device qualification.

## Verification And Historical Records

Registry tests cover global policy, exceptions, aliases, legacy migration,
installed-provider filtering, bounded helper output/deadlines and parsed
configuration. OPK tests cover native ABI, hostile paths, metadata/cache
tampering and extraction budgets. Commands and recorded host/MIPS evidence
are in [runtime tests](../tools/opendingux/README.md#verification-commands) and
[verification notes](../tools/opendingux/VERIFICATION.md). This guide records
existing evidence; it does not declare a new Alpha test run or physical boot.

The Candidate08/Candidate11 records under
`test-results/m8-candidate08-2026-10-07/` are **historical development evidence**.
Their ROM/archive/menu, scanning, selection, shutdown and disposable native-OPK
VM cases do not establish Alpha image contents, physical FPS, HDMI or audible
M8 PCM. The Native OPK QA fixture was not a shipped game/application. Longer
guest-only key holds compensated for TCG timing; they were not device input
performance measurements.

Candidate11's old spare-card geometry, rollback directory, manifest/image
hashes and "next cold boot" checklist are not current deployment instructions
and must not identify Alpha-0.0.1. Cursor follow-up in
`tools/hwtest/ui_cursor_gen10_diagnosis.md` is likewise a historical diagnosis,
not a current physical acceptance claim. Use the exact Alpha image report and
actual-device logs for delivery/acceptance; no previous candidate or VM result
certifies another board or every registered provider.
