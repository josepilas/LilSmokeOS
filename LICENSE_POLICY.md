# Licensing And Copyright

Copyright (c) 2026 José Pilas and LilSmokeOS contributors.

## Alpha-0.0.1

The entire original authored LilSmokeOS Alpha source is public under the
Mozilla Public License 2.0 in `LICENSE` and `LICENSES/MPL-2.0.txt`. This
includes the C++ UI, applications, SDK/API, shell services, board adapters,
Buildroot integration and developer tools where José Pilas holds the rights.
Daedalus-LilSmoke and custom Mupen-LilSmoke experiments are excluded.
Generic upstream Mupen64Plus/N64 support keeps its existing terms.

This is an ownership boundary, not blanket relicensing of dependencies.
Vendored source, copied routines, upstream forks and changes to upstream files
retain their applicable licenses and notices. A `lilsmoke-` prefix does not
establish MPL ownership. MIT input transport, zlib SDL dependencies, GPL engines
and LGPL libraries keep their licenses. Runtimes, fonts and art are separate.

## Earlier Releases

`LICENSES/LilSmokeOS-PreAlpha-Personal.txt` records historical Pre-Alpha terms,
not the Alpha license. `LICENSES/LilSmokeOS-MIT-legacy.txt` preserves earlier
MIT notices. Earlier permissions are not revoked. Historical notes do not
restrict the Alpha grant or promise a future private period.

## Source Distribution

The public staging tool selects the actual authored external tree and
legitimate vendored source with retained notices, not only documentation.
Its inventory records file hashes, exclusions and public-copy edits.
Private workstation paths use reviewed public placeholders; credentials are
never silently stripped. Unexpected sensitive findings block staging.
No ROMs, BIOS collections, stock exports, extracted game assets, private builds,
controller captures or user photos are covered by this grant. The user-provided
project logo and documented original generated art may be included; other art
needs individual provenance.

This snapshot is not a bit-for-bit corresponding-source claim for an older
stock-derived image. External SDKs, dependencies, proprietary boot/kernel/AVP/
driver materials and omitted assets need separate acquisition and clearance.
See [Alpha source limits](docs/alpha-0.0.1-source.md).

## Binary Distribution

Provide each component's notices, covered/corresponding source, build scripts
and relinking materials. Buildroot `legal-info` assists collection; it does not
prove all vendor or mixed-license integrations are cleared. Do not add MPL
Exhibit B incompatibility notices to original authored files.

The historical M8 image and private full-engine SM64 packages are not cleared
by this source release. The investigated SM64 renderer's source-only notice
prohibits binary redistribution; ROM ownership does not clear extracted assets.
Existing asset-free public adaptations remain separately reviewed artifacts.

See [third-party notices](THIRD_PARTY_NOTICES.md) and
[publication checks](docs/release-preparation.md). The license text and retained
component notices control; this technical policy is not legal advice.
