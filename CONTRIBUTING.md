<!-- SPDX-License-Identifier: MPL-2.0 -->
# Contributing To LilSmokeOS

LilSmokeOS is created and led by **José Pilas**. Read the
[current release draft](docs/pre-alpha-0.0.1/README.md) and
[developer guides](docs/developer/README.md) first. Older J-milestone docs are
historical planning notes, not proof of public releases or hardware support.

The tested reference target is an HCSEMI/CubeGM SEGAM-M8-V6.0 with the exact
V1.0.0 2024-12-05 stock lineage, one MIPS CPU and 128 MiB RAM. It is not a
universal Ingenic JZ4770 build. No hardware 3D/desktop stack may be assumed.

## Issues And Changes

Be respectful and specific. Use the bug, hardware-support or feature template.
The project [repository](https://github.com/josepilas/LilSmokeOS) and
[issue tracker](https://github.com/josepilas/LilSmokeOS/issues) are the selected
public collaboration locations, not a claim that this draft is released.
State the OS/build identifier, board/firmware, controller/provider and whether
evidence comes from a physical console or a substitute environment. Share
redacted excerpts only; never upload ROMs, BIOS, firmware exports, credentials,
private game captures or disk identities to an issue.

Keep changes scoped and describe motivation, behavior, compatibility risk and
tests. Prefer existing helpers/architecture over new dependencies. A useful
commit title is `ui: preserve Explorer selection after playback`. Do not claim
that a main branch, CI pipeline or untested board is always green without evidence.

For a pull request include:

- The affected feature and expected behavior, with a small reproducible test.
- Source/dependency pins and licenses for added third-party work.
- Commands/results, platform, guest resources and explicit mocks/skips.
- Memory, binary/payload size and frame-work impact when relevant.
- Corresponding user/developer documentation and changelog changes.

## Implementation Conventions

Use C++17 for existing frontend/helper code and match neighboring C/C++ style.
Target scripts must be BusyBox/POSIX `sh`; host build scripts may use their
declared Bash/Python interpreter. Quote paths/arguments, check system-call and
subprocess failures, avoid shell evaluation of metadata and refuse ambiguous
device identities. Preserve user data and handle interruption/rollback explicitly.

Use Buildroot variables and package patterns, source locks and verified hashes.
Do not hard-code a maintainer's PC path, card serial or drive letter into new
public tooling. Original-code SPDX/license declarations must match the release
audit; copied upstream files retain their notices. Do not blanket-convert engine
code to MPL merely because the surrounding application is original.

Keep software surfaces, buffers and queues bounded. Destroy launcher resources
before engine/media handoff. Measure on the target rather than assuming static
linking, fast-math, LTO, DSP or larger rendering resolution is always a win.
Avoid GL/GLES/Vulkan, external input mappers and unnecessary resident services.

## Verification And Release

See [testing and evidence](docs/developer/TESTING.md) and
[source/build](docs/developer/SOURCE-AND-BUILD.md). Scale tests with risk: parser
bounds/unit checks, host sanitizers, MIPS execution, full UI handoff and physical
acceptance for changed board interfaces. QEMU Malta is not HC16xx timing/audio
certification. Compile or menu success is not full game qualification.

No contribution should write a user's original stock card, silently format a
target, repair filesystems without backups/consent, or bypass an unsupported
board/firmware gate. Maintenance deployment scripts are not general test commands.
Keep generated/private artifacts outside a proposed source distribution.

`Pre-Alpha-0.0.1` is the intended first public release name, currently a draft.
Do not push a tag/upload an image solely because private tests pass. Follow the
[publication checklist](docs/pre-alpha-0.0.1/PUBLICATION-CHECKLIST.md), preserve
third-party obligations, and obtain release-owner approval before publication.
