<!-- SPDX-License-Identifier: MPL-2.0 -->
# TreeFrog Ports: Direct Compatibility And Converter

## Use The Direct Path First

Copy the original companion `.sh` and game folder side by side into
`LILROMS/treefrog-ports`. LilSmoke reads the folder's original `config.json`
and runs its original `run.sh` in a device-matched compatibility namespace.
This catalogue is separate from LilSmoke Applications. No conversion or
`.lilpck` installation is required for working direct ports.

The October 29 spare-card update copies the existing ports, including saves
and empty subfolders, from the historical `roms/ports` location. It preserves
that old copy. New ports should use only `treefrog-ports`.

## Browser Converter

[LilSmoke Port Converter](https://josepilas.github.io/LilSmoke-Port-Converter/)
is public, with a [0.2.0 release](https://github.com/josepilas/LilSmoke-Port-Converter/releases/tag/0.2.0)
and separate [source](https://github.com/josepilas/LilSmoke-Port-Converter/tree/source)
and [dist](https://github.com/josepilas/LilSmoke-Port-Converter/tree/dist)
branches. It is intended for ports that the native TreeFrog compatibility path
cannot handle, not as a required installation step. It is a companion browser
tool, not an on-device compiler or a prerequisite for direct compatibility.

1. Select the TreeFrog port ZIP and choose Analyze archive.
2. Select a detected port. Review its engine, ABI, dependencies, adaptation
   plan, examined strategies and warnings.
3. Choose a name/version, then Convert to .lilpck and Download.
4. Copy that package to `LILROMS/packages`, open it in LilSmoke Explorer and
   install it. The result appears in LilSmoke Applications.

The C/WebAssembly engine operates locally in a worker. Only static tool/runtime
assets are fetched; the game archive is not uploaded. Source and third-party
notices are available from the page. Packages containing personal game data
remain private. Conversion does not provide redistribution permission.

## Adaptation Contract

The planner inspects the actual ELF loader view and Libretro exports, selects
a matching glibc or musl frontend, checks transitive dependency providers,
searches same-family software-engine alternatives and examines launchers and
referenced helper executables. Unambiguous native ELF/launcher pairs can be
discovered without metadata; invalid existing metadata is not bypassed.

Converted Libretro apps bundle the independent MPL-2.0 `lilretro` host, source,
notices, selected reviewed libraries and a typed software-video/audio/input/save
policy. They do not execute the upstream PicoArch launcher. Native apps retain
their original trusted BusyBox launcher, including loops, sourced settings and
helper choices, with syntax checked before launch. They are not limited to a
list of game-specific script templates. The Libretro route still refuses setup
that its typed launch would otherwise silently discard.
The one root `.sh` may have any name. Data, scripts and original notices remain
inside that application's single `runtime/port` tree.

The supplied frontend pack does not redistribute private firmware drivers or
unreviewed SDK libc/SDL binaries. Those remain explicit matching-device OS
prerequisites. Never load a glibc engine into a musl host. A successful static
report is not a guarantee of supported engine interfaces, controls or speed.
The first launch records real capabilities in `runtime/runtime-capabilities.json`.
Executing a native core is not a security sandbox; use trusted ports.

A failed automatic route means further adaptation is required, not that every
possible manual/source port is impossible. The browser does not compile an
arbitrary source tree, translate a foreign CPU or invent a software renderer.
It reports available alternatives and build recipes without pretending to
execute unsupported ones.

## Qualification

The current converter passes 84 native/WASM regression tests, including hostile
archives and malformed metadata/ELFs. Browser qualification includes the supplied
205 MB FroggyMania archive, conversion/download and mobile layout. A real
converted FroggyMania package passes production installation and execution in
a one-CPU, 128 MiB MIPS VM, producing frames and nonzero PCM. Original Hex-a-hop,
ClassiCube and a converted native Hex-a-hop also produce frames/PCM there.

VM display/audio use a labelled QA device boundary. They do not model the HC
hardware or establish physical FPS, HDMI/DAC correctness or performance on
every supported console. The ClassiCube bridge adds right-stick digital camera
controls and avoids its missing-FrogUI return loop; its physical 3D performance
still needs controlled device testing. No broad performance fix is claimed.

Version 0.2 also converts the supplied SDL Zombies and Chocolate Doom archives
without game-specific recipes, preserving their files. Both pass browser
analysis/download tests and production converted-app and direct-copy execution
in the 128 MiB MIPS VM, with moving frames, nonzero PCM, Start+Select exit,
released mounts and a clean FAT32 check. This is functional VM evidence, not a
physical frame-rate or controller acceptance measurement.

Start+Select is held together for 500 ms on the same controller. Brief shared
input snapshot contention does not manufacture release/unplug events or fire
an exit from cached input. The bridge retains SDL analog events and chooses an
active stick pair rather than allowing a neutral first pad to mask another.
ClassiCube receives its digital camera profile. Chocolate Doom receives
horizontal right-stick turning, trigger strafing, left-stick-click run and
right-stick-click use. Its native backend does not implement vertical look or
independent dual-analog movement; user keymaps still override defaults.

Implementation: `tools/treefrog-converter/README.md` and
`tools/treefrog-compat/INTEGRATION-0.2.md`.
