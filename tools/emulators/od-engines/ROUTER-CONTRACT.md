# Native Router Contract

Checkpoint 2026-10-08. Build and source-test facts, not hardware acceptance.

Deployable tested subset: production libod-sdl.so and PrBoom with its GPL WAD.
PrBoom no-content menu passed QEMU using QA-only dummy-video SDL, not M8.
NX is candidate-only and paired Cave Story acceptance is INCOMPLETE. Never
register/deploy native-candidates/bin/nxengine merely because its menu renders.
The shared scanner's Doukutsu.exe marker is not yet a tested Evo firstload path.
Upstream nxextract is being built with rejection guards; it requires original
data/npc.tbl and Stage assets too. Real conversion/full Evo data remains untested.
The production DT_NEEDED closure and hashes are in built-native/stage-manifest.json;
runtime-loaded display modules and hardware acceptance are outside that audit.

## Paths And Calls

Optional binary root: /storage/emulators/opendingux. Binary names are nxengine,
prboom, easyrpg, scummvm. Package fallback: /usr/lib/lilsmoke/od-engines/bin/ID.
Optional auxiliary root: /storage/emulators/opendingux/data/share/ID.
No OpenBOR registration: recovered upstream requires 64-bit; M8 is o32.

All native runs: set LILSMOKE_OD_ENGINE=ID and LILSMOKE_OD_NATIVE=1.
Set LILSMOKE_HCFB=1 on actual M8 only. Parent owns trusted preload selection:
libod-sdl.so plus /os/libm8-display.so. Do not load a QA shim onto hardware.

| ID | Native home, no selected ROM | Selected content |
| --- | --- | --- |
| nxengine | No argv. Compiled-in SDL data-directory picker. LILSMOKE_NX_ROOT overrides root; default /storage/emulators/opendingux/data/share/nxengine | --data DIRECTORY. Directory contains extracted npc.tbl and engine-ready Cave Story assets. Doukutsu.exe is NOT accepted directly by this Evo binary. |
| prboom | No argv. Compiled-in SDL IWAD picker. LILSMOKE_PRBOOM_ROOT overrides root; default /storage/roms/doom | -iwad FILE. Additional PWADs use upstream -file arguments. |
| easyrpg | No argv; cwd is game-browser root (e.g. /storage/roms/easyrpg). Upstream opens game browser when cwd is not a valid game. There is NO --game-browser flag in this release. | --project-path DIRECTORY, not the RPG_RT.ldb file itself. --save-path DIRECTORY is supported. |
| scummvm | No game ID; cwd /storage/roms/scummvm. Upstream launcher includes Add Game and options. Recommended router args --gfx-mode=surface --themepath=AUXROOT. | --path=DIRECTORY GAME_ID. A .scummvm marker must be parsed to obtain its text ID and containing game directory. Do not pass marker filename as game ID. |

PrBoom engine-owned GPL auxiliary prboom-plus.wad is required and staged;
it is not a playable IWAD. No commercial/freeware game assets are bundled.
NXEngine extraction and supplied game assets remain user-owned content.
ScummVM is unfinished; no binary or classic theme is currently staged.

## Matching Backends

EasyRPG Player 0.8.1.1, commit 78328fa29f465315291e161130e6682f69410370.
Repository https://github.com/EasyRPG/Player.git. liblcf 0.8.1 release tarball:
https://easyrpg.org/downloads/player/0.8.1/liblcf-0.8.1.tar.gz.

The exact native CMake profile is in build.sh's easyrpg case. For a matching
core change PLAYER_TARGET_PLATFORM=SDL2 to libretro, add BUILD_SHARED_LIBS=ON
and CMAKE_POSITION_INDEPENDENT_CODE=ON. Required deps: liblcf, inih, fmt,
pixman, PNG/zlib, FreeType, Ogg/Vorbis. Native SDL2 is not a libretro dependency.
MIDI uses built-in FMMIDI; WAV uses built-in dr_wav. No ALSA, external synth,
MP3, Opus, XMP, HarfBuzz, JSON or Lhasa in this finite profile.
Current liblcf profile disables ICU and XML. This limits non-default text
encoding support; do not promise Japanese/all-language compatibility.

Private dependency sysroot is /root/lilsmoke-emulator-expansion-20261008/engine-sysroot.
It includes libinih.a/libfmt.a and shared pixman/Ogg/Vorbis/SDL2_mixer.
liblcf.a is still absent at this recovery checkpoint.
Use toolchain.cmake with OD_SDK and OD_SYSROOT environment, not host pkg-config.
Do not write the parent's Buildroot target tree from this build.

ScummVM 2.9.0, commit 38af1883a2eb7a622c31bfc3708888bbcc47db48.
Repository https://github.com/scummvm/scummvm.git. Native configure (NOT CMake)
accepts --opengl-mode=none --disable-opengl-game --disable-detection-full.
Selected engines: scumm,agi,sci,agos,kyra,queen,sky,lure,sword1,sword2,drascula.
Disabled all engines before selection, including sci32, scumm-7-8 and he;
no 3D engines. Exact flags are in build.sh. Backend=sdl is native; a matching
libretro build must use this version's libretro backend recipe, not SDL.
Needed native libs currently SDL2, PNG/zlib, JPEG, FreeType, Ogg/Vorbis;
no ALSA, OpenGL, FLAC, MP3, video decoders or external synths.
Final ELF NEEDED output will supersede this expected list.

## Verification

Production shim links. SDL1.2/SDL2 MIPS QEMU API tests pass with MOCK PCM.
Real PCM worker source is reused unchanged in production; hardware sound,
display, evdev/touchpad and runtime RSS/performance are NOT yet validated.
Native engine compile/CLI status is tracked in PROGRESS.md and engine-logs.
