# Publishing Pre-Alpha-0.0.1

Display name and Git tag: `Pre-Alpha-0.0.1`.
Repository: https://github.com/josepilas/LilSmokeOS .
Proposed image name: `LilSmokeOS-Pre-Alpha-0.0.1.img`. Renaming a development
image does not make it redistributable or universal.

## Required Gates

1. Set the real GitHub repository and corresponding-source location. Never
   publish placeholder URLs, personal paths, USB serials or owner logs.
2. Stage only reviewed authored/approved third-party source and notices.
   Ignore rules do not protect against force-add or direct release uploads.
3. Build pinned source/configuration. Run Buildroot `legal-info`, review every
   warning and provide matching required source/build scripts, including BSP
   modifications and modified emulator code. Collection alone is not clearance.
4. Resolve stock boot/kernel/AVP/library redistribution. Until then, do not
   upload the stock-derived `.img` or hide vendor files inside another archive.
   Supply only cleared source/tools and a documented local import process.
5. Exclude ROMs, BIOS/game-data collections, private SM64 binaries, Nintendo
   assets, stock exports and private captures. Review artwork/theme rights.
6. Complete board-specific acceptance: cold boot, partition labels/filesystems,
   first-boot growth, controllers, audio, saves, apps, emulators and safe shutdown.
   A VM is not a physical performance measurement or another-board certification.
7. Run the release audit. Failure produces a report, not an uploadable bundle.
   Freeze hashes, source commit, config, licenses and board fingerprint together.
8. Create a GitHub prerelease draft, review assets/notes, then publish only after
   the gates pass. Documentation preparation does not upload private artifacts.

## Current Blockers

- Vendor stock-derived boot stack: redistribution/source clearance pending.
- Public SM64: asset-free native relocation/loading is not complete and the
  current graphics license prohibits binary redistribution. Local packages
  stay private. An emulator is not mislabeled as a native public port.
- Upstream game-source and artwork provenance must also be reviewed; replacing
  a renderer or removing ROM bytes alone is not a blanket redistribution grant.
- Complete third-party/source/artwork audit and reproducible legal-info bundle.
- GitHub repository/source offer and public-artifact hardware acceptance.

MPL-2.0 for authored files does not remove these requirements. See
[policy](../LICENSE_POLICY.md), [notices](../THIRD_PARTY_NOTICES.md) and
[pre-alpha docs](pre-alpha-0.0.1/README.md).
