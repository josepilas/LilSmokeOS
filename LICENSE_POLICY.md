# Licensing And Copyright

Copyright (c) 2026 José Pilas and LilSmokeOS contributors.

For the initial Pre-Alpha release, original LilSmokeOS material owned by José
Pilas uses the personal-use proprietary license in `LICENSE`. The authored OS
source repository remains private in initial versions for personal reasons. A future
source publication is intended to use MPL-2.0; that intention is not a present
source grant or publication date. Public SM64-LilSmoke authored adaptation
code remains MPL-2.0, with its full license in `LICENSES/MPL-2.0.txt`.
This policy is an ownership boundary, not blanket relicensing of dependencies.

## First-Party Scope

The new personal-use license applies only to original files for which José
Pilas holds the necessary rights and has designated that license. Retained
third-party notices and permissions granted by earlier distributions prevail
for their respective files. This does not turn every source directory or
package into proprietary material. Public SM64 authored adapters retain MPL.
Applications such as the Device Adapter are covered only for their authored
code, not their bundled Python/Tcl/Tk/OpenSSL runtimes. Theme artwork, game
icons, fonts, screenshots, vendor firmware and ROM-derived data are not assumed
to be first-party software.

Source files with a retained third-party copyright/license, vendored code,
upstream forks, copied routines and modifications to upstream files keep the
applicable upstream terms. A `lilsmoke-` package name alone does not make the
downloaded library or engine MPL-licensed. `THIRD_PARTY_NOTICES.md` describes
these boundaries; individual dependency notices prevail.

Historical MIT notices are retained in `LICENSES/LilSmokeOS-MIT-legacy.txt`.
This new licensing choice does not revoke permissions already granted by a
previous MIT distribution or remove third-party copyright notices.

## Distribution

Provide MPL notices and covered source for separately MPL-covered components,
including the public SM64 authored adaptations.
Provide the exact third-party notices, corresponding source and build scripts
required by each dependency. Buildroot `legal-info` is a collection aid, not
proof that all vendor files or mixed-license integrations are redistributable.
Do not add the MPL Exhibit B incompatibility notice: interoperability with
eligible GPL/LGPL larger works must remain possible under MPL section 3.3.

The stock-derived M8 image and private full-engine SM64 packages are not public
release artifacts. The SM64 renderer explicitly forbids binary redistribution;
ROM assets and stock boot/kernel/AVP/driver files require separate clearance and
complete applicable source obligations. They are not relicensed by this file.

The pre-alpha release gate must pass before upload. Local/private build success
or ownership of a ROM dump is not itself a redistribution grant for a binary
containing extracted game data. The owner has requested publication after
implementation and review; this document does not replace artifact checks.

References: [MPL text](https://www.mozilla.org/en-US/MPL/2.0/),
[Mozilla FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/),
[Buildroot legal-info](https://buildroot.org/downloads/manual/manual.html#legal-info).

This project policy documents technical release requirements, not legal advice.
