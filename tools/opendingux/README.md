# Bounded Native OpenDingux Runtime

This runtime reads real SquashFS v4 OPKs without loop devices, mounts,
FUSE, libopk's whole-file extractor, shell command evaluation, or a fake
legacy loader. It is a native compatibility subset, not an OpenDingux OS
emulator or a sandbox for untrusted native machine code.

## Sources and ABI

- Streaming library: [squashfs-tools-ng v1.2.0](https://github.com/AgentD/squashfs-tools-ng/tree/v1.2.0),
  exact commit f2a3ad56e40c9711b23371238f9fa07dd24245f1, LGPL-3.0+.
  Private library build caps each allocation at 1 MiB and the XZ decoder
  at 8 MiB. No worker threads or CLI tools are required at runtime.
- Actual [upstream libini](https://github.com/pcercuei/libini), commit
  8797a7b5b70a40883a0a0e8dcd874ad74db62587, LGPL-2.1+.
  Unmodified sources/license are vendored and linked as libini.so.0.
  Desktop input is bounded before calling ini_open_mem, with a sentinel
  to avoid the upstream parser's trailing-whitespace EOF edge.
- SHA256 uses the already installed libsodium incremental APIs.
- ELF32 little-endian MIPS32/R2 O32, legacy FP32 ABI, musl interpreter
  /lib/ld-musl-mipsel.so.1 or static ET_EXEC. DSP1/DSP2 are accepted for
  the verified 74Kc target. N32, MIPS64, FP64/NaN2008, other ASEs,
  uClibc/glibc interpreters, shell scripts, private/GPU dependencies,
  and custom loader search/audit/filter paths are rejected.
- Known installed native dependencies are allowlisted, not arbitrarily
  loaded from packages. Every embedded ELF is inspected. This is not
  proof that all SDL APIs, peripheral ioctls, timings or memory needs of
  an arbitrary upstream app are supported. Source ports should use the
  shipped software SDL1.2 compatibility or SDL2 layer.

On the M8, the launcher restores only the fixed OS-owned
/os/libm8-display.so adapter after sanitizing the environment, plus the
validated framebuffer pixel mode and native-video/input flags. Arbitrary
inherited preloads and package loader paths remain forbidden. The M8 has
no ALSA device: ordinary SDL audio uses dummy pacing, while source-adapted
engines must use the native PCM worker for audible output. This layer is
not an automatic audio/ioctl translator for arbitrary legacy binaries.

## Catalog and CLI

Catalog roots are /usr/share/lilsmoke/opendingux/apps and
/storage/opendingux/apps. Only immediate .desktop/.opk files are imported;
OPKs may contain multiple nested desktop entries. Scan reads filesystem
metadata, bounded desktop contents and ELF prefixes/tables; it never
extracts a package and never executes a compatibility probe.

scan outputs exactly five TSV fields:

    id<TAB>name<TAB>systemsCSV<TAB>reason<TAB>needsRom

Reason is empty for apps passing the native policy. Rejected apps remain
in the catalog with a diagnostic. IDs are od- plus 64 hex SHA256 digits,
derived from catalog source path, NUL, and inner desktop path; content
updates preserve the same app's HOME. PocketSNES's fixed ID is in
INTEGRATION.md for parent-side deduplication.

run ID [ROM] rescans, checks compatibility, and executes argv using
fexecve, never sh -c or eval. %f/%F/%u/%U take one optional absolute,
already-resolved ROM. A ROM field makes needsRom=1; absence of that ROM
is a diagnostic. %% becomes a literal percent. Quotes, escaped spaces
and literal arguments are supported within explicit bounds. Embedded or
unknown field codes, shell syntax, custom Path/TryExec and Terminal=true
are rejected. Native imported desktops use relative Exec or a rebuilt
binary under /usr/lib/lilsmoke/od-emulators. Only the exact packaged
PocketSNES desktop can call the existing trusted od-launch wrapper.

Source-rebuilt metadata example:

    [Desktop Entry]
    Type=Application
    Name=Native SNES Port
    Exec=bin/emulator %f
    X-LilSmoke-Systems=snes
    Terminal=false

X-OpenDingux-Systems and X-OpenDingux-System are fallback aliases.
Systems use lowercase ASCII tokens separated by commas or semicolons.
Bare %F/%U mean one ROM here, not unrestricted multi-file/URL expansion.

## Resource and Filesystem Limits

- 128 records; output below 128 KiB; 4-second scan deadline.
- 16 KiB desktop, 32 argv slots, 511 bytes per argument.
- 256 MiB compressed image, 128 KiB block, 4096 nodes/fragments/IDs,
  depth 16, 120-byte ASCII components, 1023-byte path.
- 128 MiB per file, 512 MiB expanded package and total cache,
  32 KiB streaming copy buffers. Production helper AS limit: 48 MiB,
  restored before application exec. No helper whole-image/file/tree buffer.
- 30-second run preparation budget; a slow/large package fails explicitly.
- Component-wise openat/O_NOFOLLOW. Traversal, unsafe FAT names,
  case-fold collisions, links, devices, FIFOs and sockets are rejected.
  SquashFS hard-linked regular inodes are materialized as independent
  files; no host hard links are created.
- Atomic mkdir + PID-owner cache lock, without flock. Normal failures and
  handled signals release the owned lock. SIGKILL/power loss can leave a
  stale lock; a dead PID produces a diagnostic. Remove cache/.lock only
  while runtime is stopped. No unsafe concurrent stale-lock reclamation.
- Source SHA256 cache key; private temporary payload, fsync, completion
  marker, atomic rename. Every reused payload file is compared streaming
  against the OPK, including size/type/link checks and unexpected-node
  detection. Failure never publishes an incomplete app.
- Cache is /storage/opendingux/cache, on a writable executable mount.
  FAT noexec is diagnosed. There is no automatic cache eviction: remove
  unused entries while stopped if the 512 MiB budget is exhausted.
- Per-ID HOME is /persist/opendingux/home/ID. The existing built-in
  PocketSNES wrapper retains its established /persist/saves/pocketsnes.
  Imported applications receive a clean software-SDL environment.

## Verification Commands

Run from WSL as root, with source tree P and Buildroot output B as in the
task. All QA outputs default to /root/lilsmoke-opendingux-qa; no full UI
or core build, target replacement, or card write is performed.

    sh tools/opendingux/build-isolated.sh host
    sh tools/opendingux/build-isolated.sh mips
    sh tools/opendingux/check-unit.sh
    python3 tools/opendingux/test_runtime.py --helper /root/lilsmoke-opendingux-qa/host/runtime/lilsmoke-opendingux --library-path /root/lilsmoke-opendingux-qa/host/runtime:/root/lilsmoke-opendingux-qa/host/sysroot/usr/lib
    python3 tools/opendingux/test_runtime.py --helper /root/lilsmoke-opendingux-qa/mips/runtime/lilsmoke-opendingux --library-path /root/lilsmoke-opendingux-qa/mips/runtime:/root/lilsmoke-opendingux-qa/mips/sysroot/usr/lib:/root/lilsmoke-os/build/buildroot/output/staging/usr/lib --qemu /usr/bin/qemu-mipsel --sysroot /root/lilsmoke-os/build/buildroot/output/staging
    sh tools/opendingux/check-installed-catalog.sh
    sh tools/opendingux/build-native-fixture.sh

Hosted QA flags enable test roots, skip host-system dependency lookup,
and bypass QEMU-user's inappropriate host AS limit/root remapping.
They are NEVER enabled by the production Makefile or copied to target.
check-installed-catalog uses the actual production target tree read-only,
with a QA-only setrlimit shim because QEMU applies guest AS limits to its
much larger host process. This does not simulate stock-kernel syscalls.

## Disposable VM Fixture

native-qa.opk is entirely original test code, no ROM/game assets. It uses
native musl/SDL2 and software rendering only: 320x240 logical colored
tiles, moving progress bar, arrow-key selection, and native event logging.
Exclusive fullscreen uses a bounded current display mode (not
FULLSCREEN_DESKTOP). Screen overrides are validated at 320..1920 by
240..1080; defaults are 854x480 or 1280x720 on HCFB.
It exits on Enter+RightShift, common Start+Select controller mappings,
Escape, or a 45-second timeout. HOME/opk-qa.log contains
OPK_QA_START, READY, KEY/JOY/PAD, CHORD/TIMEOUT and EXIT evidence.

Parent copies /root/lilsmoke-opendingux-qa/fixture/native-qa.opk ONLY into
the disposable VM's /storage/opendingux/apps, uses the production helper,
launches through UI, captures rendered pixels and sends native input.
Catalog/extraction tests alone do not prove the physical M8 display,
controller, stock kernel or application execution. Parent owns this VM
integration and any eventual card deployment.
