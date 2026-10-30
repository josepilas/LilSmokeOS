# LilSmoke Port Converter 0.2.0

Author: José Pilas. First-party code: MPL-2.0; see LICENSE.
This is the ready-to-serve `dist` branch root. Keep all directories and
`.nojekyll`. The entry point is `index.html`; all assets use relative URLs.

For `josepilas/LilSmoke-Port-Converter`, configure GitHub Pages to deploy from
branch `dist`, folder `/ (root)`. The source branch is `source`. The parent
owns Git operations, Pages setup and the `0.2.0` release. This directory contains
no hosting identity, credentials, SDK, firmware, private QA input or game data.

Complete converter and independent lilretro source, headers, tests, build tools
and notices are in `source/LilSmoke-Port-Converter-0.2.0-source.zip`.
`SHA256SUMS.json` inventories the files in this tree (excluding itself).
`THIRD-PARTY-NOTICES.md` and `licenses/` retain third-party terms.

The separate publication runtime ZIP removes nonloadable debug/file symbols
and sanitizes compiler path metadata. Its manifest describes every included
file, original build-input hashes, and original/published ELF identities.
ELF load segments remain identical except section-table bookkeeping in the
ELF header. Original frozen binaries and the old runtime ZIP are untouched.
Device libc/SDL/driver dependencies remain external. Conversion and API/browser
tests do not guarantee hardware performance or gameplay compatibility.
