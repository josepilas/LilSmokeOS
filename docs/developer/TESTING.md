<!-- SPDX-License-Identifier: MPL-2.0 -->
# Testing And Evidence

## Separate Claims

| Test class | What it can establish | What it cannot establish |
| --- | --- | --- |
| Host unit/sanitizer | Parser, bounds, arithmetic, lifecycle and memory-safety regressions | MIPS ABI or hardware speed |
| MIPS user emulation | ISA/ABI execution and compatible linking | Full kernel, physical display/audio or timing |
| QEMU Malta full UI | Actual MIPS userspace/launch/input/control flows under configured limits | HC16xx cycle-accurate FPS or native PCM/HDMI behavior |
| Physical M8 | Actual tested firmware/card/peripheral/game combination | Universal board/controller/game compatibility |
| Card readback | Verified bytes and preservation against a baseline | Successful physical boot or power-loss immunity |

Always state CPU count, guest RAM/process cap, backend, fixtures, mocks and
skipped hardware checks. A rendered frame counter is not automatically effective
game speed; a menu opening is not gameplay qualification. Dummy/mock audio does
not count as audible native sound.

## Existing Test Areas

- `tools/apps/`: package lifecycle, malformed archives, recovery and actual
  installer/application views. QA failpoint builds must not ship.
- `tools/opendingux/`: metadata/ELF/resource/cache rejection and native OPK
  execution checks. Legacy loader/GPU incompatibility must remain explicit.
- `tools/hwtest/`: core probes, shared input/ROM scanning/storage/service tests,
  theme rendering and UI workflows. Some fixtures are private or device-specific.
- `tools/media/`: synthetic legal media, models, FFmpeg/SDL playback controls,
  ASan/UBSan, EOF restart and full MIPS Explorer handoff.
- `tools/device-adapter/tests/`: stock readonly export, identity/platform guards,
  plan/apply/rollback and interrupted transaction handling.
- `tools/portsmoke/sm64-original/`: private original-engine regression, quality/
  scaler/span tests and full-engine MIPS launch checks. Do not publish its
  generated assets, screenshots/audio or restricted executable.

Read a harness's options and dependencies before running it. Do not substitute
a production target for a disposable fixture or run maintenance/SD scripts as
a generic test suite. Keep original stock files and ROMs read-only and hash them
before/after private tests. Never inject keyboard input into the Windows desktop
to control a guest; isolate test controls to the guest/API/owned SDL process.

## Physical Acceptance

For a spare card, record profile/image hashes and verify backups first. Test
boot, TV aspect/colors, each intended controller mode and analogs, scanning,
plain/compressed content, own provider menu, Start+X, Start+Select, sound,
save/reload and safe shutdown. Test the new feature rather than assuming it
inherits a prior version's pass. For media use small known-compatible files
before demanding codecs. For SM64 profile actual whole-frame work and audio
delivery before claiming improved detail without a performance regression.

Test USB storage ownership, expansion and interruption only with disposable
fixtures/media and stable recovery access. Verify that ROMs/settings/saves and
protected boot files are preserved after updates. Do not edit the original card.

## Public Reports

Record build/source pins, exact commands, platform, fixture licensing, results,
failures and untested paths. Sanitize private paths, host names, account secrets,
MAC/USB/disk identities and proprietary dependencies. Prefer synthetic media/
homebrew fixtures for public reproducers. Publish no commercial-ROM captures by
default. Source hashes and aggregate test counts are useful without exposing
the owner's card or game data. Link claims to reviewed evidence, not old notes.

Before release, follow the
[publication checklist](../pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md). Buildroot
license collection, test passes and card readback do not independently clear
vendor or third-party redistribution rights.
