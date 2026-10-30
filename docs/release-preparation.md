# Publishing Alpha-0.0.1

Display name and Git tag: `Alpha-0.0.1`.
Repository: https://github.com/josepilas/LilSmokeOS .
Original authored Alpha source is MPL-2.0; upstream terms remain unchanged.
Historical Pre-Alpha documentation and permissions remain available.

## Source Gates

1. Run `tools/release/stage_alpha_source.py` and its tests. Review the complete
   inventory, exclusions, public-copy edits and redacted secret audit.
2. Include the C++ UI, shell/services, Buildroot tree, SDK/API and authored
   tools, not just docs. Exclude named custom N64 experiments, private data,
   outputs/caches and unreviewed assets.
3. Preserve upstream notices, corresponding-source obligations, legacy grants,
   creator credit, public logo/banner and issue templates.
4. Inspect the snapshot/archive for private paths, credentials, controller/disk
   identities, ROMs, stock exports and game-derived art.
5. Freeze hashes after concurrent source authors finish. Include the separate
   image builder and updated MBR-aware VM QA once frozen.
6. Publication is a separate owner-controlled action. Preserve Git history;
   do not force-push or upload from the development workspace.

## Independent Image Gates

Buildroot `legal-info`, exact per-component source/build/relinking materials,
stock boot/kernel/AVP/driver clearance and art/font provenance remain separate
requirements. Source publication is not binary redistribution clearance.
No ROM, BIOS collection, private game build or stock export may be attached.

The Alpha image uses separately verified runtime binaries plus release metadata;
source staging does not rebuild them or establish bit-identical provenance.
Record actual geometry, inventories/hashes and source/configuration pins.
Do not reuse the old Pre-Alpha checksum as an Alpha checksum.
Physical Alpha cold boot, expansion, input/audio, games/saves and safe shutdown
must be tested or clearly marked unverified. QEMU is not M8 certification.

See [source limits](alpha-0.0.1-source.md), [policy](../LICENSE_POLICY.md)
and [third-party notices](../THIRD_PARTY_NOTICES.md).
