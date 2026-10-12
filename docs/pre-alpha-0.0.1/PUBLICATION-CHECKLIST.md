<!-- SPDX-License-Identifier: MPL-2.0 -->
# GitHub Publication Checklist

**Release gate: not passed.** This is a documentation handoff for the release
owner, not an upload instruction or a claim that an image is public-ready.
Use the requested name `Pre-Alpha-0.0.1` when the approved release is made.
The selected [GitHub repository](https://github.com/josepilas/LilSmokeOS) and
[release index](https://github.com/josepilas/LilSmokeOS/releases) are known.
Do not invent a version-specific tag, source offer or download before it exists.

## Source And Rights

- [ ] Audit only a clean, explicitly selected source distribution; do not upload
  the whole development workspace or an old project ZIP.
- [ ] Apply MPL-2.0 to verified original LilSmoke files/notices and resolve legacy
  root/component license metadata without changing upstream terms.
- [ ] Record each dependency's source URL, immutable version/commit, license,
  patches and binary provenance. Resolve unknown/reconstructed-code rights.
- [ ] Supply required corresponding source, build scripts/configuration, license
  texts and any relinking materials with a stable source location for binaries.
- [ ] Review art, cursor sprites, fonts, app icons and bundled themes separately.
  Reference images and popular theme downloads are not automatic license grants.
- [ ] Exclude ROMs, BIOS, commercial assets, private screenshots/audio/videos,
  generated asset-bearing SM64 sources/binaries and stock-card exports.
- [ ] Establish redistribution clearance for every included vendor boot/kernel/
  AVP/driver file, or deliver a genuinely sanitized base/local import workflow.
  A Device Adapter folder does not substitute for a required raw boot region.
- [ ] Resolve SM64's source-only renderer and asset-free loader requirements
  before listing a public SM64 `.lilpck`. Omit it if still blocked.
- [ ] Scan both payload and repository history for credentials, host keys,
  usernames/private paths, disk/USB identities and private firmware artifacts.

## Artifact And Hardware

- [ ] Build the approved payload from the pinned source/configuration. Name the
  artifact consistently (for example `LilSmokeOS-Pre-Alpha-0.0.1.img`) and record
  architecture, profile, partition formats/sizes and included providers/tools.
- [ ] Independently inspect the image's MBR/filesystems and verify both LILOS
  and LILROMS inventories/checksums. Do not rename a private image and call it
  sanitized without auditing its actual sectors and filesystem contents.
- [ ] Publish SHA256 for the final artifact and source tag. Do not reuse a
  checksum from an earlier Candidate image or infer one from its filename.
- [ ] On a spare device card, test flashing, required local import, first boot/
  expansion/recovery, controller modes, scanning, games, saves and safe shutdown.
- [ ] Test media audio/playback on the physical M8; classify other engines and
  features by actual evidence instead of extrapolating from compile/menu passes.
- [ ] Test or explicitly label macOS adapter and every additional board/profile
  as unverified. No universal M8/JZ4770 compatibility claim.
- [ ] Exercise interrupted update/import recovery on disposable media, retain
  verified backups and document the supported end-user update path.
- [ ] Confirm final docs match the actual payload, default settings and image
  geometry; remove placeholder download/source locations before publication.

## Release Page

- [ ] Review [release notes](RELEASE-NOTES.md), installation, known issues,
  [license scope](LICENSES.md) and [credits](../../CREDITS.md).
- [ ] Create the intended GitHub source tag/release only after explicit owner
  approval and passed gates; mark it **Pre-release**.
- [ ] Attach only approved distributable artifacts, checksums and source/notices.
  Do not include owner-private test packages or firmware exports.
- [ ] Provide the exact supported firmware lineage, backup warning, no-ROM/BIOS
  statement and documented limitations prominently in the release description.

## What This Documentation Does Not Do

It does not change licenses in source, produce a sanitized `.img`, authorize
redistribution of vendor firmware, complete the public SM64 runtime, flash a
card or publish a GitHub release. Those are separate release-owner operations.
