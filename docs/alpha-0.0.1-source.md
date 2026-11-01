<!-- SPDX-License-Identifier: MPL-2.0 -->
# Alpha-0.0.1 Source Distribution

For runtime features and changes since Pre-Alpha, read the
[Alpha release notes](alpha-0.0.1/RELEASE-NOTES.md). This document describes
source staging, not the complete user-facing changelog.

The initial release source ZIP, its tag and SOURCE-INVENTORY/AUDIT reports
are a frozen publication snapshot. Later documentation-only corrections on
`main` do not silently replace that archive or change its hashes. Corrected
documentation is also published as a separate release asset.

## Source Build Correction R1

On November 1, 2026 the missing `configs/lilsmoke_*_defconfig` files were
restored for QEMU, SF3000, GB350 and both VirtualBox profiles. The source export
now accepts Buildroot's `_defconfig` naming convention, with binary/credential
checks unchanged. The wrapper has target listing, configure-only mode and
separate Buildroot/output overrides; focused regressions protect the workflow.

Use current `main` or `LilSmokeOS-Alpha-0.0.1-source-r1.zip` for new builds.
The R1 archive is exported from the public Git revision and adds a new source
inventory/revision record. It does not pretend that the original inventory
described files omitted by that initial export. The original image, tag and
source ZIP/checksum remain unchanged. See [build instructions](build.md).

All five profiles were configured and their recipe graphs evaluated with
pinned Buildroot 2024.02.3 in disposable outputs. This does not certify a
complete clean package compile, VirtualBox ISO boot or any hardware BSP.
SF3000/GB350 full builds remain intentionally gated; recorded upstream
DirectFB/musl configuration warnings are not suppressed.

Created and led by **José Pilas**. Original authored Alpha source is public
under MPL-2.0. Upstream source/fork modifications retain their own terms.
Daedalus-LilSmoke and custom Mupen-LilSmoke experiments are excluded; generic
upstream Mupen64Plus and unrelated names are not excluded.

## Contents

The distribution includes actual C++ UI/application code, shell services,
media/input/terminal/storage helpers, board adapters, Buildroot recipes,
configurations/patches, SDK headers/examples/API and developer tools.
Legitimate vendored emulator/engine/library source keeps its notices.

The owner's existing public project logo and Ko-fi banner are preserved.
Original generated PCSX4ALL artwork has generation provenance in the tree.
Lucide assets retain their license. Other icons, themes, fonts and photos are
omitted unless separately reviewed and allowlisted. Missing resources may
need to be supplied or replaced for builds.
The owner-provided UI cursor sheet is included exactly. Textual dependency
tables, bitmap-font source, shaders and build-template data are source inputs,
not automatically excluded as media. Their retained component terms apply.

## Staging

```sh
python -B -m unittest discover -s tools/release -p "test_alpha_source.py" -v
python -B tools/release/check_license_text.py
python -B tools/release/check_license_scope.py
python -B tools/release/stage_alpha_source.py
```

Default checkout: `tools/release/_build/alpha-source`.
Archive: `tools/release/_build/LilSmokeOS-Alpha-0.0.1-source.zip`.
Reports alongside the checkout record every selected file's SHA-256/size,
public-copy edits, excluded paths and redacted secret findings/rationale.
A pruned directory entry covers its entire subtree without traversing it.
Staging never uploads, contacts GitHub or touches a physical card.
The prior public checkout is an optional read-only input for its logo and issue
templates, never a source of Git metadata or unrelated files.
Release metadata is read as it currently exists, without overriding publication
status. Report `upload_performed: false` describes the staging tool's own actions,
not whether a separately controlled GitHub release has happened.

## Exclusions

Build/work/test-result trees, Git metadata, caches/sourcekits, binaries, local
SDKs, ROM/BIOS images, stock exports, extracted assets, user media, controller
captures, logs and private development checkpoints are excluded. Unreviewed
binary assets nested in upstream trees are excluded too. Source preparation
scripts remain; their game-derived outputs do not. See the exclusion report
for the precise asset list rather than assuming all resources are present.

Retained authored scripts/examples use reviewed public placeholders for
personal workstation paths. Credentials are not made safe by stripping text:
unexpected secret-shaped values block staging. Generic target/SDK paths,
synthetic fixtures and audit detection patterns have separate rationale.
Historical Pre-Alpha docs do not define Alpha licensing.

## Exact-Source Limits

This is a sanitized snapshot of actual authored source, not a claim that all
older payloads reproduce byte-for-byte. Pinned external dependencies, toolchains,
stock BSP/kernel/AVP/driver materials and omitted art need separate acquisition
and rights review. Source staging does not rebuild verified runtime binaries.
The independent Alpha image must record its own inventory, hashes and
source/configuration provenance. Physical Alpha cold boot remains unverified
until explicitly tested. See [policy](../LICENSE_POLICY.md) and
[third-party notices](../THIRD_PARTY_NOTICES.md).
