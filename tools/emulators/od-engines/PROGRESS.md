# Native Engine Recovery Checkpoint

## Current Recovery: Production Subset

Fresh checks on 2026-10-08, after the second shutdown. Previous sections below
are history, not current acceptance. OD_JOBS=1 plus a shared native-build lock.
No card/target/router/UI writes. No sources or build objects deleted.

- Production libod-sdl.so refreshed from adapter objects; SHA256
  a4d067af3b47f18bac4bba344a925c3f94904450ba2097296853bf3dc3fbd206.
  Fresh production SDL1.2/SDL2 QEMU tests pass generic audio passthrough and
  reject forced QA/native flags without verified hcfb. No mock PCM symbols.
- PrBoom --help and actual source-compiled no-content menu pass QEMU:
  320x240, 1818 differing pixels, clean SDL_QUIT exit. Required
  prboom-plus.wad (357768 bytes) and COPYING are present in built-native.
  QA uses a separately built SDL2 dummy-video library and capture preload;
  neither is staged. Pixel probe now rejects uniform multibyte-color frames.
- stage-manifest.json records production binary/asset hashes and recursive
  DT_NEEDED source paths. Two ELFs, six libraries, two auxiliary/license files;
  no missing libraries. Runtime-loaded display modules are not covered.
- NX source has an upstream nxextract CMake target. Build now produces ONLY
  native-candidates/bin/{nxengine,nxextract}; no deployable NX binary.
  Candidate extractor patch adds missing/truncated/non-MZ/original-data
  rejection and fails resource CRC errors instead of silently writing them.
  Real user-owned Doukutsu.exe conversion has NOT been tested. The marker
  alone lacks npc.tbl/Stage assets; Evo also needs its full auxiliary data.
  Paired Cave Story acceptance is INCOMPLETE, regardless of menu test results.
- Parent reprioritized finite production subset and matching EasyRPG deps.
  Only this agent's NX process group was interrupted near 47%; completed
  objects preserved. NX/extractor candidates are NOT built or tested yet.
  liblcf now compiles alone; generated ldb_database command verified to end
  with -O1. liblcf.a is still absent until that phase completes. ScummVM deferred.
- Newly wired native package defaults to prepare/deps/PrBoom only. Future
  engines require explicit LILSMOKE_OD_ENGINES_BUILD_STEPS override; no default
  giant ScummVM build and no NX candidate installation.
- Real M8 display, vendor PCM, physical time64evdev, gameplay and <=110 MB
  runtime acceptance remain untested. Mock-PCM QA is not hardware QA.

## Resumed After Cleanup

User resumed development. Preserve all objects/stages; ONE heavy compile at
a time, OD_JOBS=1 enforced. No card/runtime/UI writes. OpenBOR stays hidden.
Initial recheck: sources/objects survive, no compiler active, PrBoom staged;
liblcf.a still absent. Shim objects are newer than staged libod-sdl.so and
must be relinked/tested. Earlier stop checkpoint below is historical.

## STOP CHECKPOINT: Parent WSL Recovery

2026-10-08 user instruction: NO NEW BUILDS until parent explicitly resumes.
WSL IPC has failed with 0x8007274c under concurrent compilers. Parent may
terminate Ubuntu-24.04. Do not kill unrelated processes or restart the distro.
On resumption build ONE ENGINE ONLY, OD_JOBS=1. Do not run NXEngine, liblcf
and ScummVM simultaneously. Use -O1 for liblcf's large generated ldb_database
translation unit instead of -O2. Preserve all existing objects/logs/checkouts.

Last observed facts, not a fresh WSL probe:

- /root/lilsmoke-emulator-expansion-20261008/built-native/bin/prboom exists;
  MIPS QEMU --help passed. Source-native picker compiled into it.
- Native menu pixel test did NOT pass with production SDL: SDK has DirectFB
  only and disables dummy/offscreen video. QA-only dummy SDL build was started
  under engine-build/sdl-qa, unfinished at this checkpoint. NEVER deploy it.
- /root/.../built-native/libod-sdl.so exists from an earlier successful link.
  SDL1.2/SDL2 MIPS QEMU QA passed with mock PCM. A later production refresh
  failed because exported private-sysroot -I exposed SDK header warnings to
  -Werror; a fix (empty CPPFLAGS for shim) was submitted immediately before
  interruption. Whether its following refresh completed is UNCONFIRMED.
  Production binary freshness MUST be checked before restoring it to hardware.
- Last NXEngine progress approximately 25%; its source-native menu patch
  was not yet applied to the running original build. Do not advertise the
  future --data/no-content contract as a tested finished NXEngine binary.
- Last liblcf progress approximately 45%; liblcf.a was not installed yet.
- ScummVM configure succeeded with no OpenGL/ALSA and corrected DirectFB
  include path; native object compilation underway, no executable verified.
- PrBoom stage helper reached its binary copy but a live-edited build script
  subsequently reported unexpected EOF. Auxiliary WAD/license staging must
  be checked and completed incrementally; do not assume them present.
- Some build driver invocations predate the script snapshot guard; preserve
  their outputs but recheck final stage steps after parent recovery.
- LAUNCH-CONTRACT.json and ROUTER-CONTRACT.md contain exact source pins,
  stage paths, menu/game argv, cwd, dependency profile and matching backend
  instructions. Unfinished fields are intentional.

Only modified package/lilsmoke-od-sdl, package/lilsmoke-od-engines and
tools/emulators/od-engines. No device/card writes or parent UI/router edits.
OpenBOR remains unbuilt/unregistered: recovered source requires 64-bit.

Updated 2026-10-08 after interrupted PC power loss.

## Scope

Only package/lilsmoke-od-sdl, package/lilsmoke-od-engines and this directory.
No card/device writes, no parent UI, registry, scripts, Config.in or external.mk edits.
Build scratch and deliverables: /root/lilsmoke-emulator-expansion-20261008.

## Recovered Evidence

- All ten existing SDL package files read. Original adapter-build.log read.
- Adapter compilation stopped at events.c:150 (-Werror=misleading-indentation).
  Existing frontend.o and audio.o preserved; no complete shim existed at recovery.
- Five upstream source repositories survived under sources/{nxengine,prboom,easyrpg,scummvm,openbor}.
- Eleven parent-owned built-libretro ELF outputs and their ABI/build-info files survived.
  Do not overwrite those outputs.
- SDK: /root/lilsmoke-os/build/buildroot/output/host/bin/mipsel-buildroot-linux-musl-.
- Staging already contains SDL2 2.28.5, sdl12-compat 1.2.68, SDL2_image,
  SDL2_ttf, libpng, JPEG and FreeType. SDL1.2 addons not yet verified.
- Native engine output: none verified at this checkpoint.

## Contract

- 74Kc MIPS32r2/o32 musl hard-float FP32; stock kernel 4.4 input_event is 16 bytes.
- Reuse Input.cpp/Keymap.cpp and isolated m8_pocketsnes_audio.c 44100 S16 PCM worker.
- Public SDL1.2 API interception is required because sdl12-compat resolves SDL2 directly.
- Guard native mode with actual hcfb verification; generic Linux must pass through.
- Native home entries enter an app-owned menu without a ROM argument. Any required
  picker belongs in the native source, never in OS UI or an external input mapper.
- No bundled game data; retain licenses for any engine-owned auxiliary resources.
- No arbitrary uClibc OPK compatibility claim; OpenBOR remains optional and unverified.

## Next Verification

Resume the adapter in its existing scratch directory; verify exported APIs and ABI.
Pin recovered upstream commits before selecting minimal dependency builds.
Record actual build commands, needed shared libraries and no-content CLI tests here.
Hardware audio/display/menu acceptance remains a parent-owned test, not implied by ELF success.

## Build Checkpoint 2

- Production shim links with -Werror; staged /root/.../built-native/libod-sdl.so.
- MIPS QEMU SDL1.2 and SDL2 QA passed: generic hardware gate/pass-through,
  callback U8/22050 mono -> S16/44100 stereo, pause/locking, bounded queue,
  frame alignment, PCM failure rejection, pointer events and Ctrl+F5 modifiers.
  The QA library has a mock PCM sink and is NEVER staged as trusted library.
- Pins for parent matching backends:
  EasyRPG Player 0.8.1.1 = 78328fa29f465315291e161130e6682f69410370;
  liblcf 0.8.1 release source. Player supports PLAYER_TARGET_PLATFORM=libretro.
  ScummVM 2.9.0 = 38af1883a2eb7a622c31bfc3708888bbcc47db48.
  Curated engines scumm,agi,sci,agos,kyra,queen,sky,lure,sword1,sword2,drascula;
  no OpenGL, 3D engines, full-engine detection or external MIDI synths.
  NXEngine-Evo = 1f093d1423cc395eb199230cd609b806ef1daa36.
  PrBoom-Plus 2.6.66 = 969515162c5aebea4ad7a125ee178dcad3d576ad.
- OpenBOR recovered 787b6770409935137579715febf80cf7a529b748 requires 64-bit
  in upstream CMakeLists.txt. No guard bypass; no binary or success claim.
- SDK is read-only. Dependency builds install into engine-sysroot, a private
  copy; no Buildroot output/target changes. Commands and logs are in build.sh
  and /root/.../engine-logs/. Original interrupted logs are preserved.
