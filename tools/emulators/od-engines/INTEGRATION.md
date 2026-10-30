# Native Engine Integration Contract

Disjoint owned paths: new package/lilsmoke-od-sdl/,
package/lilsmoke-od-engines/, tools/emulators/od-engines/ only.
No existing OPK/registry/UI/libretro/external/target/card files changed.

- OS-owned /usr/lib/lilsmoke/libod-sdl.so, gated by exact
  LILSMOKE_OD_NATIVE=1 plus LILSMOKE_HCFB=1 and a verified hcfb framebuffer.
- One adapter handles public SDL1.2 and SDL2, including SDL12-compat's
  private SDL2-handle bypass. Do not substitute a dummy audio device.
- Trusted preload chain must preserve /usr/lib/libm8-display.so:
  libod-sdl.so first, then that existing display helper; no private loader.
- Native audio reuses existing m8_pocketsnes_audio.c and m8-audio.c;
  input reuses current Input.cpp, Keymap.cpp and Log.cpp with package-local
  stock-4.4/time64 evdev wire definition. No outside input mapper.
- Engine IDs: nxengine, prboom, easyrpg, scummvm, openbor. Only actual
  verified staged binaries may be registered as available.
- Optional binaries: /storage/emulators/opendingux/<engine>.
  Auxiliary data: /storage/emulators/opendingux/data/share/<engine>.
  Engine saves/config: distinct per-engine folder under /persist/od-engines.
- Content: NXEngine Cave Story directory; PrBoom IWAD .wad;
  EasyRPG directory with RPG_RT.ldb/.lmt; ScummVM game directory/target;
  OpenBOR .pak. No game assets are bundled.
- NX candidates are withheld from built-native. Doukutsu.exe conversion and
  full Evo data must pass before paired Cave Story registration/deployment.
  Current production subset, tests and hashes: PROGRESS.md, LAUNCH-CONTRACT.json
  and built-native/stage-manifest.json. Hardware acceptance is not implied.
- Start+Select emits SDL_QUIT for engine cleanup, not forced kill.
  Start+X uses the known engine menu key, otherwise Pause; never fake states.
- Set LILSMOKE_OD_ENGINE to the exact ID for engine-specific key bindings.
  Parent owns registry pairing/content resolution and the launch environment.
- SDL1.2, SDL2, PNG/JPEG/zlib are already staged. SDL_mixer/codecs and
  EasyRPG liblcf/pixman/fmt dependencies are assessed separately; keep all
  shared SDK dependencies on LILOS, optional binaries/data on storage.
- Isolated persistent root: /root/lilsmoke-emulator-expansion-20261008.
  Nothing is staged directly into Buildroot target or the user card.
