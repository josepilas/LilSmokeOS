<!-- SPDX-License-Identifier: MPL-2.0 -->
# Credits

## Project Creator

**José Pilas** is the creator, designer and project lead of LilSmokeOS. He
defines its behavior, visual direction, hardware goals and application ecosystem,
provides the development hardware and carries out the physical-device feedback
and acceptance tests. The initial authored OS release uses a personal-use
proprietary license; public SM64 authored adaptations use MPL-2.0. Previous
permissions and upstream terms remain in force.

Codex assisted with implementation, research, porting, testing and documentation.
This does not transfer authorship of upstream engines, commercial games or
vendor firmware to LilSmokeOS.

## Operating System And Libraries

Thanks to the authors and maintainers of
[Linux](https://www.kernel.org/), [Buildroot](https://buildroot.org/),
[BusyBox](https://busybox.net/), [musl](https://musl.libc.org/),
[GCC](https://gcc.gnu.org/), [SDL](https://www.libsdl.org/),
[DirectFB](https://github.com/DirectFB/directfb),
[FFmpeg](https://ffmpeg.org/), [libarchive](https://www.libarchive.org/),
[libwebp](https://chromium.googlesource.com/webm/libwebp/),
[FreeType](https://freetype.org/), [libpng](http://www.libpng.org/pub/png/libpng.html)
and [zlib](https://zlib.net/), and their contributors. These supply the
toolchain, userspace, software display, decoding, archive and image foundations.
Optional network/device tooling also builds on Dropbear and BlueZ.

PC tools use [Python](https://www.python.org/), [Tcl/Tk](https://www.tcl.tk/),
[PyInstaller](https://pyinstaller.org/), and their bundled runtime dependencies.
Preserve the exact delivered runtime notices rather than replacing them with
the project license. Tests use QEMU and sanitizer/developer tool communities.

## Emulator And Native Engine Communities

Thanks to [RetroArch/libretro](https://github.com/libretro/RetroArch),
PCSX4ALL contributors and its PCSX/gpu_unai/SPU lineages,
[PCSX ReARMed](https://github.com/libretro/pcsx_rearmed),
PocketSNES/Snes9x, ReGBA/gpSP, mGBA, PicoDrive, Genesis Plus GX, FCEUX/FCEUmm,
Gambatte, QuickNES, FBNeo, Mednafen/Beetle, Handy, Stella, ProSystem, Atari800,
RACE, PokeMini, Oswan/Gnuboy, MAME4ALL, PrBoom, NXEngine/Cave Story,
EasyRPG/liblcf, ScummVM and OpenBOR developers and contributors.

Exact libretro repository/commit pins are recorded in
[`package/retroarch-mips/cores.lock`](package/retroarch-mips/cores.lock) and
[`package/lilsmoke-libretro-extra/cores.lock`](package/lilsmoke-libretro-extra/cores.lock).
This list acknowledges used/investigated lineages; it is not a promise that
all engines ship or have physical gameplay qualification.

Thanks to [OpenDingux](https://github.com/OpenDingux), its SDL/input/packaging
community, [libopk](https://github.com/pcercuei/libopk), and libini/libsquashfs
contributors whose formats/interfaces informed the native integration.

## Hardware Research

Research and reconstruction were informed by 700zx1's
[mini_rkgame](https://github.com/700zx1/mini_rkgame),
[sf3000toolchain](https://github.com/700zx1/sf3000toolchain),
[sf3000-dev](https://github.com/700zx1/sf3000-dev),
Siarhei Volkau's [sf3k-hacking](https://github.com/SiarheiVolkau/sf3k-hacking),
[game-de-it/sf3000](https://github.com/game-de-it/sf3000), and Witali's
[r36sx_disasm](https://github.com/Witali/r36sx_disasm), alongside the HC16xx/
CubeGM community. Research credit does not grant rights to redistribute
proprietary firmware or imply that these authors certify LilSmokeOS.

## SM64 Development And Design References

Thanks to [n64decomp/sm64](https://github.com/n64decomp/sm64) contributors for
the original-engine decompilation foundation and
[DrUm78/sm64-port](https://github.com/DrUm78/sm64-port) and its FunKey/GKD
lineage for the investigated CPU port. The graphics directory credits **Emill
and MaikelChan** and restricts binary redistribution; retain its notice.
Nintendo and the original game creators retain rights in the game/assets.
Private test builds and captures are not public release content.

Theme compatibility draws on EmulationStation/RetroPie, ArkOS and EmuELEC
formats and communities. Themes and their authors retain their licenses.
LilSmoke Explorer takes interaction inspiration from 351Files. Application
installation/channel design takes inspiration from
[FBI](https://github.com/Steveice10/FBI), Wii Menu and GameCube Menu, not a claim
of Nintendo/FBI assets or source ownership. Referenced artwork/cursors/icons
require individual provenance checks before publication. Lucide-derived assets
retain their original notices where used.

## Notice

This is an attribution overview, not a complete license bill of materials.
Per-component source notices and the final release audit are authoritative.
No affiliation or endorsement by credited projects, Nintendo or device vendors
is implied. See [license scope](docs/pre-alpha-0.0.1/LICENSES.md).
