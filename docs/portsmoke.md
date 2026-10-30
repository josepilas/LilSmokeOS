# PortSmoke

PortSmoke is LilSmokeOS' native PC ports runtime. It is inspired by
PortMaster, but constrained for the SF3000/GB350 class of devices: MIPS32,
128-256 MB RAM, no GPU, SDL2 through DirectFB, ALSA audio, musl, and BusyBox.

PortSmoke ports are not launched through input mappers or desktop wrappers.
Engines must be built for software rendering and patched/configured for the
handheld controls before packaging.

## Storage Layout

`/storage` is LILROMS: exFAT by default, with manual FAT32 formatting supported.
Save paths must not depend on filesystem symlink support.

```text
/storage/portsmoke/
├── engines/
│   └── devilutionx.peng
├── ports/
│   └── diablo/
│       ├── DIABDAT.MPQ
│       ├── cover.webp
│       ├── portsmoke.conf
│       └── Diablo.sh
└── saves/
    └── diablo/
```

`.peng` files are SquashFS engine images. Each image should contain the port
engine binary under `bin/` and any private runtime libraries under `lib/`.

## First Game Development

The optional `BR2_PACKAGE_PORTSMOKE_SM64` package starts the Super Mario 64
BYO-data port. Its native MIPS extractor, private indexed asset cache, scanner
metadata and launch flow are tested. **It is not yet a playable engine**:
`sm64-byo-dev.peng` contains only data-preparation tools and the development
script reports missing gameplay support after extraction. It is off by default.
See [the source study and implementation status](flash-image-and-sm64-byo.md).

## Runtime Flow

1. LilSmokeUI scans `/storage/portsmoke/ports/*/portsmoke.conf`.
2. The UI shows each valid port under the `PortSmoke` system.
3. Selecting a port launches `/usr/bin/portsmoke-launch <folder>`.
4. The router reads `EngineFile` and `Script` from `portsmoke.conf`.
5. The router mounts `/storage/portsmoke/engines/<engine>.peng` at
   `/mnt/portsmoke/<engine>/`.
6. The port script sets SDL/ALSA environment, links saves, and runs the engine.
7. The router waits, unmounts the engine, and removes the mount point.

## Metadata

```ini
[PortSmoke]
Name=Diablo 1
EngineFile=devilutionx.peng
Script=Diablo.sh
Cover=cover.webp
Architecture=mips32r2
ABI=o32-hard-float
Libc=musl
Renderer=software
MinimumMemoryMB=128
```

`ABI` and `Libc` are compatibility boundaries, not descriptive labels. The
stock SF3000 development SDK produces glibc-linked MIPS o32 binaries; those
libraries must not be copied into a LilSmokeOS `.peng`. Rebuild every engine
and bundled shared library for LilSmokeOS' musl, MIPS32r2, hard-float toolchain.
The launcher and UI reject metadata that explicitly requests another ABI or
libc. Metadata made before these keys were introduced remains accepted.

`Script` and `EngineFile` must be local filename components. Paths, parent
directory traversal, and unusual shell characters are intentionally rejected by
the router.

`Architecture`, `Renderer`, and `MinimumMemoryMB` are optional compatibility
guards. When present, the router rejects non-MIPS32r2 engines, hardware-3D
renderers, and ports whose declared minimum RAM exceeds the device.
`MinimumMemoryMB` is compared with Linux `MemTotal`, not the RAM advertised on
the box. Leave room for kernel/reserved memory when targeting a 128 MiB device.

## Game Script

Use `package/portsmoke/examples/game_template.sh` as the starting point. Keep
arguments as quoted positional parameters with `set --` and launch with:

```sh
exec "$ENGINE_BIN" "$@"
```

This avoids BusyBox shell word-splitting surprises and keeps port scripts
predictable on low-memory targets.

Because `/storage` is normally FAT32, the template first tries the portable
`saves` symlink and then uses a temporary bind mount when symlinks are not
supported. The bind mount is always removed when the engine exits or receives a
termination signal.
