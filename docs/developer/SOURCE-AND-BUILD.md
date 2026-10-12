<!-- SPDX-License-Identifier: MPL-2.0 -->
# Source And Build Guide

## Repository Map

| Path | Role |
| --- | --- |
| `Config.in`, `external.mk`, `external.desc` | Buildroot external tree registration |
| `configs/` | Development defconfigs and optional fragments |
| `board/segam-m8-v6-stock/` | Stock-preserving M8 launch/native hardware adapters |
| `board/qemu-mipsel/` | Malta development guest; not a physical M8 BSP |
| `package/lilsmoke-ui/` | C++17 frontend, themes, Explorer and application views |
| `package/lilsmoke-scripts/` | ROM routing/resolution, service and launch helpers |
| `package/lilsmoke-apps/` | Bundled `.lilpck` runtime/install/catalog support |
| `package/retroarch-mips/`, `package/lilsmoke-libretro-extra/` | Frontend/core recipes and source locks |
| `package/pcsx4all/`, `package/lilsmoke-opendingux/` | PSX/native compatibility sources |
| `package/lilsmoke-media/`, `package/lilsmoke-ffmpeg/` | Software player and decoder recipes |
| `package/lilsmoke-sm64-original/` | Original-engine platform adapter; private build restrictions |
| `tools/device-adapter/` | PC stock-folder importer and tests |
| `tools/m8/`, `tools/hwtest/`, `tools/apps/`, `tools/media/` | Maintenance and QA tooling |

Generated `build/`, `work/`, local stock exports and private test assets are
not source-release inputs. Do not publish the working directory wholesale.

The owner-provided project repository is
[josepilas/LilSmokeOS](https://github.com/josepilas/LilSmokeOS). A repository URL
does not establish that a particular release's complete corresponding source
or approved artifact has already been uploaded. Use the reviewed source tag
and final source inventory when a release is actually published.

## Build Host

Buildroot requires a Linux build environment. Windows can use WSL2; macOS needs
a Linux VM/container for the OS build. The cross-platform PC adapter is a
separate Python/Tk project. Use a case-sensitive filesystem for the Linux build
and a path without spaces. Plan sufficient disk space for downloads and toolchains;
there is no reliable universal ten-gigabyte/full-build-time guarantee.

The wrapper pins Buildroot 2024.02.3. Check package pins, `.hash` files, fragments
and the **resolved** Buildroot `.config` rather than assuming a defconfig contains
every later development change. Clean independent output trees prevent target
mixing. Do not destroy an existing private SDK to try a fresh configuration.

## Development Build

From an approved clean source checkout in Linux:

```sh
LILSMOKE_JOBS=1 bash scripts/build.sh qemu-mipsel
bash scripts/run-qemu.sh
```

These are the existing development wrapper/runner, not a certified production
M8 image recipe. The runner requires the generated images and framebuffer
configuration it expects. Review its options and inspect boot logs if startup
fails. A successful compile is not a boot/playability test.

Within a correctly configured Buildroot SDK, a focused package rebuild uses
its external-tree path, for example:

```sh
make -C /path/to/buildroot BR2_EXTERNAL=/path/to/lilsmoke-os lilsmoke-ui-rebuild
make -C /path/to/buildroot BR2_EXTERNAL=/path/to/lilsmoke-os legal-info
```

`legal-info` helps collect licenses/source, but it cannot certify unknown vendor
binary rights or repair incomplete package license metadata. Review its output.
Older handheld configs are gated by `scripts/check-real-hardware-bsp.sh`;
do not bypass that gate to describe an unverified BSP as ready.

## M8 Build Boundary

The M8 private image builder preserves the stock boot region and approved
dependencies. It needs stock-image provenance even when stock files come from
a recovered folder. Building a generic rootfs/MBR or copying a DTB is not enough.
No reproducible **public, sanitized** M8 image procedure is certified by this
guide while the [publication gate](../pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md)
is open. Internal Candidate update scripts contain maintainer-specific identity
guards; they are not portable end-user flashing commands.

Keep target ABI consistent: little-endian MIPS32r2/musl and the pinned float/
calling conventions. Do not apply DSP, fast-math, `-mno-shared`, LTO or other
flags blindly to every library. Executable renderer optimizations do not imply
those flags are suitable for shared objects. Avoid GL/GLES/Vulkan and desktop
stacks. Bound caches/queues and release UI resources before running an engine.

## Device Adapter Development

From `tools/device-adapter/`:

```sh
python3 -m unittest discover -s tests -v
python3 launch.py inspect /path/to/stock
```

Inspection is read-only; keep exports private. Run a platform's identity/GUI
tests before claiming its install support. Frozen Windows delivery must include
its runtime notices; Linux/macOS still require Python 3.10+ and Tk. See
[stock import](../pre-alpha-0.0.1/STOCK-IMPORT.md) for the user contract.
