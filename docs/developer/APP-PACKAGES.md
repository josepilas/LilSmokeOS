<!-- SPDX-License-Identifier: MPL-2.0 -->
# Native Application Package Guide

## Root Layout

A `.lilpck` is a supported content-detected archive (ZIP/7z/tar variants) with
no wrapper directory. Bundle the application's non-platform runtime instead
of relying on the old shared `.peng` engine scheme.

```text
meta.json
run.sh                    # exactly one root .sh
icon.webp                 # optional
runtime/bin/my-app
runtime/lib/...
runtime/assets/...
licenses/...
```

The installed directory is `/storage/independent-apps/<name>/`. Subdirectories
may contain multiple scripts; the application root may not. Archive links,
special files, traversal and FAT case collisions are rejected. Do not depend
on POSIX symlinks or executable permissions surviving FAT/exFAT extraction;
bundle ordinary files and use the validated launcher contract.

## Metadata

```json
{
  "id": "org.example.my-app",
  "name": "My App",
  "version": "1.0.0",
  "icon": "icon.webp",
  "author": "Example Author",
  "description": "A native software-rendered application",
  "architecture": "mipsel-linux-musl",
  "license": "MPL-2.0"
}
```

`name` and `version` are mandatory. Use a stable `id` across versions/renames.
The optional license field is descriptive metadata, not proof of payload rights.
Icons may be WebP/PNG/JPEG/BMP up to 512x512 and 2 MB; smaller is preferable.
Names must be FAT-safe (no reserved names, control characters, slash, trailing
dot/space or `:*?"<>|`) and at most 120 UTF-8 bytes. One to three numeric version
components plus optional prerelease/build suffixes are accepted. `1`, `1.0` and
`1.0.0` compare equally; build suffixes do not establish upgrade order.

## Launcher

The helper sets working directory, `LILSMOKE_APP_DIR`, `LILSMOKE_APP_NAME`,
`HOME` and runtime library paths. A minimal original script can be:

```sh
#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd -- "${LILSMOKE_APP_DIR:?Application launcher required}"
mkdir -p ./saves
export LD_LIBRARY_PATH="$PWD/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec ./runtime/bin/my-app
```

Build little-endian MIPS/musl binaries with the approved SDK, not Windows, ARM
or arbitrary legacy OpenDingux binaries. Use SDL software rendering/DirectFB;
clear desktop display assumptions. M8 native display/audio/input adapters are
required instead of assuming every board has ALSA or Ingenic IPU controls.
The application owns its UI and must handle quit/release resources cleanly.
Keep textures/queues bounded and do not introduce a permanent input mapper.

Create mutable saves/settings at runtime rather than including them as
package-owned defaults. Stable app IDs preserve provider selections on rename.
Uninstall removes user data inside the app folder too. A package update retains
user-created paths but incoming package paths take precedence and old owned
files are removed. Never overwrite a user's saves merely to change a launcher.

## Emulator App Contract

Declare a canonical supported ID or array:

```json
{"open_core": ["gba", "md"]}
```

This fragment belongs inside `meta.json`, not in a second document. IDs/aliases
are defined in `package/lilsmoke-scripts/src/EmulatorSystems.h`. Only declare
systems actually supported by the bundled engine. Unknown IDs/null/non-string
entries/empty arrays are refused. The app appears as **LilSmoke App: My App**
in those systems' settings and is stored by stable `app:<id>` selection.

ROM launch gives two quoted arguments `core=gba` and `rom=Game Name.gba` plus
environment variables `core`, `rom`, `LILSMOKE_OPEN_CORE`, `LILSMOKE_OPEN_ROM`.
The name is system-relative and can include a nested directory. Resolve it
below `${LILSMOKE_ROM_ROOT:-/storage/roms}/<core>/`; no absolute ROM path is sent.
For example, a launcher's ROM branch can use:

```sh
if [ -z "${core:-}" ]; then
    exec ./runtime/bin/my-emulator --menu
fi
case "$core" in gba|md) ;; *) exit 2 ;; esac
input="${LILSMOKE_ROM_ROOT:-/storage/roms}/$core/${rom:?ROM filename required}"
resolved=$(/usr/lib/lilsmoke/lilsmoke-rom "$core" "$input") || exit $?
exec ./runtime/bin/my-emulator --system "$core" "$resolved"
```

Adapt the engine's actual command-line syntax; those engine options are example
placeholders. Never `eval` metadata/arguments, split names on spaces, or execute
a game-data EXE. Opening the app from the grid provides no core/ROM arguments
and should open its own menu. The declaration is not a compatibility layer for
a system the engine cannot emulate.

## Package And Test

From the directory containing the package root files on a development PC:

```sh
zip -r ../My-App-1.0.0.lilpck meta.json run.sh icon.webp runtime licenses
```

Omit `icon.webp` if absent. For 7z use a modest 1-8 MiB LZMA2 dictionary;
compression ratios are less important than fitting extraction RAM. The helper
has a 64 MiB address-space ceiling, streams 64 KiB blocks, caps entries at
20,000 and expanded data at 8 GiB, with a FAT-safe single-file limit below
4 GiB. Staging updates also need space for retained user data.

Test inspect/install/cancel, own-menu launch, compatible ROM handoff, input,
sound, save persistence, version comparison, rename, obsolete-launcher cleanup,
uninstall and interrupted recovery on disposable storage. See
[testing](TESTING.md). Include upstream licenses and required source/build
materials; do not classify a GPL-derived emulator as wholly MPL. Private SM64
assets/source-only renderer binaries are excluded from public packages.
