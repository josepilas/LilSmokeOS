# OpenDingux Integration

## Navigation and Policy

- The home screen has an OpenDingux entry listing native engines and installed
  applications. Emulator entries open a compatible-game list; applications
  without a content argument launch directly. Incompatible packages remain
  visible with a diagnostic instead of being represented as working engines.
- Settings > Emulator Core Settings: left/right changes the global backend
  between RetroArch and OpenDingux and clears per-system exceptions.
- Pressing this setting opens all thirteen supported emulated systems,
  including systems without ROMs. Press a system to choose an installed
  compatible engine or Follow global setting. Per-system choices can use a
  different backend from the global default.
- Policy applies to every system routed by lilsmoke-launch. No installed
  compatible engine means a visible launch error, not an implicit fallback.
- Before the user explicitly changes global policy, existing PocketSNES and
  PCSX4ALL selections are migrated to explicit per-system exceptions under
  the RetroArch default. Settings and execution use the same
  provider registry; stale/missing engine overrides are refused.

Configuration stores EMULATOR_BACKEND=retroarch|opendingux and optional keys
such as EMULATOR_snes=od:pocketsnes or EMULATOR_gba=ra:mgba. Empty per-system
values follow the global policy. Metadata is never evaluated by a shell.

## Hardware Boundary

The real SEGAM-M8-V6 is HC16xx MIPS 74Kc, one Linux CPU, roughly 110 MiB
usable RAM, Linux 4.4.186, and musl userspace. It is not an Ingenic JZ4770
board. Original OpenDingux often expects an Ingenic display/input stack,
uClibc, and loop-mounted OPK files. The stock M8 has no loop or FUSE support
and flock is unavailable. Those interfaces cannot be assumed.

OpenDingux's package execution behavior is documented in upstream
[opkrun.c](https://github.com/pcercuei/libopk/blob/master/opkrun.c). Its SDL
fork also adds nonstandard video flags in
[SDL_video.h](https://github.com/OpenDingux/SDL/blob/opendingux/include/SDL_video.h).
Native MIPS/musl source rebuilds using standard software SDL are supported;
arbitrary legacy OPKs are not universally binary-compatible. A musl loader
must never be symlinked under the name of a uClibc/glibc loader.

The new OPK layer uses upstream libini metadata parsing and libsquashfs
streaming rather than mounting packages or allocating complete files. Import
locations and detailed accepted ABI/package rules are documented alongside
the runtime sources and tests. ROM archives use the existing shared bounded
resolver before emulator handoff. ROMs and proprietary assets are not shipped.
The M8 launch path preserves the fixed native display adapter and evdev
mapping after removing untrusted preload/search-path variables. Because this
kernel lacks ALSA, a source port needs the native PCM adaptation for sound;
passing ELF/package checks alone does not prove all application behavior.

## Verification

Registry tests cover global policy, exceptions, legacy choices, installed-core
filtering, aliases, malformed catalog records, output limits, child deadlines,
and quoted/commented configuration. Host and MIPS/musl runs pass. Existing
MIPS archive and native menu/damage mock-device tests also pass.

End-to-end VM and physical deployment results are recorded separately under
test-results/m8-candidate08-2026-10-07. A 128 MiB single-CPU MIPS VM is functional
QA, not a faithful model of the HC16xx framebuffer, audio device, or timing.
Do not interpret VM measurements as actual game/cursor FPS on the M8.

The complete run in
test-results/m8-candidate08-2026-10-07/opendingux-native-final/result.json
passed: 7z SNES, ZIP/plain GBA and Mega Drive, three menu/resume cycles per
variant, manual scanning, controller exits, safe shutdown, global/per-system
selection and clearing overrides. A separate original native OPK was opened
from the home UI, streamed to its FAT cache, rendered without GPU APIs,
accepted arrow/Start/chord input, and saved its per-app HOME journal.
The OPK is a disposable QA fixture, not a game or an app shipped on the card.

The OPK layer also passed nineteen host and nineteen MIPS hostile-fixture
tests, plus sanitizer/unit checks. Tested rejections include legacy/GPU
dependencies, traversal, links, FAT case collisions, corrupt metadata,
tampered cache contents, and resource limits. Exact commands and caveats
are in tools/opendingux/README.md.

Cursor follow-up is documented in tools/hwtest/ui_cursor_gen10_diagnosis.md:
native retries, exact opaque bulk fills and a visible-span framebuffer mapping
are ready. The previous device log cannot identify the initial C rejection
branch, so the next physical boot must confirm recovery and cursor timing.

## Candidate11 Deployment

Installed on the identity-verified spare 62,534,975,488-byte USB card:
33 new runtime objects and 629 reused objects hash-verified. Stock kernel
and the user's ROM hash unchanged; no partition writes or formatting.
Rollback: D:/lilsmoke/rollback/before-candidate11.
Active manifest SHA256:
9720f708864926d3eb04192231eaa075630e4577e364f9d2847c7ff81580a527.

Generated image SHA256:
ad1b8943331defcf75ab020f387b4e2b877f399940770cd32cef8237c34da2f6.
The final M8 environment/migration/native OPK follow-up passed in
test-results/m8-candidate08-2026-10-07/opendingux-m8-env-confirmed/result.json.
Test input chords use longer guest-only holds to span slow TCG software
frames; the physically confirmed RetroArch input code was not altered.

Put native-compatible OPKs/desktops in LILROMS/opendingux/apps. Arbitrary
legacy uClibc/GPU packages still require native source adaptation/rebuild.
The disposable Native OPK QA application is not installed on the real card.
Safely eject and cold-boot the spare card to check the touchpad, OpenDingux
home entry, and global/per-system emulator choices on the actual M8.
