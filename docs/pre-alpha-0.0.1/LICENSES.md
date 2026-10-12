<!-- SPDX-License-Identifier: MPL-2.0 -->
# License Scope And Distribution

Initial releases use a **personal-use proprietary license** for original OS
material owned and designated by José Pilas. Its source remains private in
initial versions for personal reasons; future source publication is intended
to use MPL-2.0. Public SM64 authored adaptations remain **MPL-2.0**. Earlier
permissions are not revoked. This is **not blanket relicensing of the Linux
distribution or upstream software**. See [LICENSE](../../LICENSE),
[license policy](../../LICENSE_POLICY.md) and
[third-party notices](../../THIRD_PARTY_NOTICES.md). The release audit owns
per-component notice/provenance reconciliation; this guide does not overwrite
an upstream license or declare that every payload file is cleared.

For MPL-covered distributed files, retain applicable notices, provide the
covered source/modifications and tell executable recipients where to obtain
that source. New original files should have an appropriate SPDX notice. Consult
the [MPL text](https://www.mozilla.org/en-US/MPL/2.0/) and
[Mozilla FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/) for authoritative terms;
this summary is not legal advice or a substitute for the license.

## Third-Party Components

Upstream copyright, license texts and source-distribution obligations remain
in force. Recipes/pinned source notices, not this overview, determine the final
per-file and executable obligations. Representative components include:

| Component / role | Review requirement |
| --- | --- |
| Linux, BusyBox, Buildroot | Preserve their GPL terms and applicable corresponding source |
| RetroArch | Preserve its GPLv3-or-later source/license obligations |
| PCSX4ALL and native/core engine code | Preserve original engine/plugin notices; audit reconstructed portions |
| Libretro cores, OpenDingux ports | Review each pinned engine separately; licenses vary |
| FFmpeg native decoder build | LGPL-2.1-or-later plus applicable bundled-component notices; configuration matters |
| SDL2, DirectFB, musl, archive/image/font libraries | Include exact-version notices and applicable source/relinking materials |
| Device Adapter frozen runtime | Include Python, Tcl/Tk, OpenSSL, zlib-ng and packaging-runtime notices as applicable |
| Theme/icon/font/art assets | Verify provenance and redistribution permission separately |

Linking or copying upstream code does not grant authority to replace its
license with MPL. GPL-derived engine modifications need compatible terms;
separate original adapters may have their own notices. A `.lilpck` must carry
the licenses and required source/build materials for **its entire payload**.

## Excluded From Public Artifacts

Original stock-card files/exports may include vendor kernels, AVP modules,
drivers and proprietary boot components without established redistribution
clearance. Ownership of a card is not permission to publish its contents.
Keep these in local private adaptation workflows until individually cleared.
ROMs, BIOS files, commercial game data and private captures are excluded too.

The current SM64 renderer lineage has a separate source-only license that
prohibits binary redistribution. Its private asset-bearing builds cannot be
uploaded as release applications, even with a BYO-ROM description. A public
version requires a distributable renderer and asset-free runtime design.
See the [pinned graphics notice](https://github.com/DrUm78/sm64-port/blob/a173bd0a3eeed9b064a234e528f128424d46ea08/src/pc/gfx/LICENSE.txt).

Credits are attribution, not license grants or endorsements. See
[publication checks](PUBLICATION-CHECKLIST.md) and [credits](../../CREDITS.md).
