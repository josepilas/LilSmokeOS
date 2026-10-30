# LilSmoke Applications

LilSmoke Applications replaces the PortSmoke home tab. Its native, software-rendered
grid and package installer are independent of EmulationStation themes. Existing
PortSmoke files are left intact; this change does not delete old user data.

## Package Format

A `.lilpck` is an ordinary supported archive with a different extension. ZIP,
7z, tar, tar.gz, tar.bz2, tar.xz and tar.zst are detected from their contents,
not the extension. Do not put a wrapper folder around the package:

```text
Button-Tester-1.0.0.lilpck
  meta.json
  run.sh
  icon.webp                 # optional
  runtime/
    bin/application
    lib/...                 # bundled engine/runtime libraries
    assets/...
```

Exactly one `.sh` must be directly at the archive root. Subfolders may contain
any number of scripts. The installer refuses links, special files, path traversal,
case-colliding entries and multiple root launch scripts. Executable files must be
little-endian MIPS Linux binaries compatible with the device's musl toolchain.
An archive extension does not make ARM, Android, Windows or glibc binaries work.

```json
{
  "id": "org.example.my-game",
  "name": "My Game",
  "version": "1.2.0",
  "icon": "icon.webp",
  "author": "Example",
  "description": "A native application",
  "architecture": "mipsel-linux-musl"
}
```

`name` and `version` are mandatory. `icon` is optional and relative to the root.
PNG, JPEG, BMP and WebP icons up to 512x512 and 2 MB are accepted. Missing or
invalid icons display a question mark. Keep `id` unchanged in future releases:
it identifies an app even after the user renames it. If omitted, an id is derived
from the original name, so later releases must retain that original package name.
Unknown metadata fields are preserved.

Names must fit FAT32/exFAT: no slashes, control characters, Windows reserved
names, trailing spaces/dots, or `:*?"<>|`. The limit is 120 UTF-8 bytes.
Versions support one to three numeric components, optional prerelease and build
suffixes. `1`, `1.0` and `1.0.0` compare equally; build suffixes do not affect order.

## Installation And Updates

Open the package in LilSmoke Explorer. The installer shows its icon on a small
CPU-projected translucent cube, name, version, INSTALL and CANCEL. Cancel returns
to the same Explorer view without installing. During installation, the blue bar
shows percentage and processed decimal MB. It reaches 100% only after commit.

Same version: **Already installed.**

Newer or older version: **This package is a different version from yours. Would
you like to update?** Either direction requires explicit INSTALL confirmation.

Installed layout:

```text
/storage/independent-apps/
  My Game/
    meta.json
    run.sh
    runtime/...
    saves/...               # created by the app, not bundled defaults
    .lilsmoke-install.json   # package-owned file inventory
/storage/.lilsmoke-apps/     # lock, staging and recovery journal; outside app grid
```

An update stages a complete replacement, overwrites/adds incoming package files,
removes obsolete files owned by the previous package, and preserves user-created
files and empty data directories. Incoming package paths take precedence. The
old root launcher is removed even if its name changes from `uno.sh` to `duo.sh`.
Keep saves and mutable user configuration out of the package's owned file set.

The swap is journaled and flushed; recovery restores the old app if interrupted
before commit or completes cleanup after commit. A sudden power loss can still
damage FAT storage: always use the console's safe Power Off before unplugging.
Recovery is performed before catalog or modifying operations. Installation is
blocked while LILDRIVE is exported to a computer.

Y opens Rename, Uninstall and Cancel. Rename changes the folder and `meta.json`
name but retains the id. Updates preserve the user's renamed name. Uninstall
requires confirmation; deleting the app folder manually also removes it from the
catalog. User data inside that folder is deleted on uninstall.

## Runtime Contract

The app runs in its own folder with `LILSMOKE_APP_DIR`, `LILSMOKE_APP_NAME` and
`HOME` set. `runtime/lib` and `lib` are added before inherited library paths.
Bundle engine binaries and their non-platform runtime dependencies in the
package. The OS supplies musl, DirectFB and device services; `.peng` engines are
not required or mounted. Packages are native programs, not sandboxed: install
only trusted packages. Applications can access user storage and device services.

```sh
#!/bin/sh
set -eu
cd -- "${LILSMOKE_APP_DIR:?Application launcher required}"
export LD_LIBRARY_PATH="$PWD/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./runtime/bin/application
```

SDL uses DirectFB and software rendering with DISPLAY/Wayland cleared. ALSA is
the default audio path on supported boards; the real M8's native PCM and hcfb
adapters are used instead on HC16xx. Its shared input adapter supplies the
configured gamepad/keyboard mappings, Start+X menu request and Start+Select quit.
Applications must process SDL_QUIT and release resources on exit. Generic
bindings: arrows, A=Left Ctrl, B=Left Alt, X=Space, Y=Left Shift, L=Tab,
R=Backspace, Start=Return, Select=Escape. M8 app surfaces are capped at 640x480;
the existing 2D display scaler handles TV output without a 3D API.

## Emulator Applications

An application can declare one or several supported system IDs:

```json
{
  "id": "org.example.multi-emulator",
  "name": "My Emulator",
  "version": "1.0.0",
  "open_core": ["gba", "md", "n64"]
}
```

`"open_core": "gba"` is also valid. IDs and aliases come from
`package/lilsmoke-scripts/src/EmulatorSystems.h`; aliases are normalized and
duplicates removed. Unknown IDs, null, non-string entries and empty arrays are
rejected. Keep the app's id unchanged. It appears under **Emulator Core Settings**
for each declared system as **LilSmoke App: My Emulator**. Selecting it stores
`app:org.example.multi-emulator`, not its folder name. Renaming therefore preserves
the selection. Updating or deleting the app refreshes availability when Settings
is opened again; unavailable explicit choices fail instead of silently launching
another engine. The global RetroArch/OpenDingux setting remains unchanged.

Opening a ROM sends exactly two quoted arguments to the application's root script:
`core=gba` and `rom=Game Name.gba`. The same values are available in environment
variables `core`, `rom`, `LILSMOKE_OPEN_CORE` and `LILSMOKE_OPEN_ROM`. The OS does
not send the absolute ROM path. The default folder is `/storage/roms/<core>/`;
`LILSMOKE_ROM_ROOT` overrides that base for custom deployments and tests. Nested
games receive a system-relative filename, such as `subfolder/Game Name.gba`, to
avoid ambiguity. Archive filenames are passed unchanged; use the OS archive
resolver below if the bundled emulator requires an extracted file.

```sh
#!/bin/sh
set -eu
cd -- "${LILSMOKE_APP_DIR:?Application launcher required}"
if [ -z "${core:-}" ]; then
    exec ./runtime/bin/my-emulator --menu
fi
case "$core" in gba|md|n64) ;; *) exit 2 ;; esac
input="${LILSMOKE_ROM_ROOT:-/storage/roms}/$core/${rom:?ROM filename required}"
resolved=$(/usr/lib/lilsmoke/lilsmoke-rom "$core" "$input") || exit $?
exec ./runtime/bin/my-emulator --system "$core" "$resolved"
```

Normal launches from LilSmoke Applications still open the app's own menu and
receive no emulator arguments. Do not `eval` arguments or split filenames on
spaces. The OS validates containment, readable regular files, declared systems
and unambiguous installed app IDs before execution. Bundled applications remain
trusted native code, not a sandbox. This declaration does not make an app
compatible with a system it cannot actually emulate.

PCSX4ALL is a preinstalled application (`org.lilsmoke.pcsx4all`, `open_core=psx`),
not an OpenDingux channel entry. Its engine, native controls, menu and persistent
save/configuration paths are retained. Old `od:pcsx4all` settings migrate to the
application ID. RetroArch's PCSX ReARMed remains a separate per-system option.
The OS seeds bundled applications through `S08applications` before the UI starts.
Seeding preserves renamed apps and user data, never downgrades a user package,
and honors intentional uninstall rather than reinstalling it at every boot.

The installer uses a separate helper with a 64 MB address-space ceiling,
streamed 64 KB blocks, at most 20,000 entries, 8 GiB total expanded data and a
FAT-compatible single-file ceiling below 4 GiB. Staged updates need free space
for the replacement plus preserved user data. Use modest compression dictionaries
(7z LZMA2 1-8 MB, moderate zstd window sizes) for 128 MB devices.

## Build And Tests

Buildroot package: `BR2_PACKAGE_LILSMOKE_APPS`, installed helper:
`/usr/lib/lilsmoke/lilsmoke-apps`. CLI: inspect, install, list, rename,
uninstall, run and recover. UI and helper communicate with bounded NDJSON.

`tools/apps/build_demo.py` produces Button Tester packages with their MIPS
binary, SDL2/TTF runtime and font bundled. No ROM, game asset or BIOS is included.
`tools/apps/test_packages.py` runs lifecycle and hostile-archive tests on host or
MIPS user emulation; `test_recovery.py` requires a QA-only failpoint build.
`test_fat.sh` uses a disposable loop image, never physical media. `test_ui.sh`
tests real SDL views at 854x480, 640x480 and 320x240; the progress screenshot is
a deliberate 41% layout fixture, not a claimed real installation measurement.
`candidate_vm_smoke.py --applications` exercises the full production UI through
QMP in a private one-CPU/128 MB MIPS VM. Malta/Cirrus is not cycle-accurate HC16xx
hardware and cannot establish real-device frame rate, audio or HDMI behavior.

Design references: [FBI](https://github.com/Steveice10/FBI),
[libarchive's API examples](https://github.com/libarchive/libarchive/wiki/Examples),
and [Semantic Versioning](https://semver.org/). No FBI source is incorporated.
