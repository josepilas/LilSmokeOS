<!-- SPDX-License-Identifier: MPL-2.0 -->
# Alpha-0.0.1 Source Distribution

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
