# License And Attribution Notice

Undertale-LilSmoke 0.0.1 is a mixed-license source distribution. The default
runner license is AGPL-3.0-only, not MPL-2.0. Original copyright and per-file
notices remain controlling; no upstream code has been relicensed.

| Files / component | License / boundary |
| --- | --- |
| `src/`, `audio/`, `build.py`, `test.py`, `test_package.py`, `fixture.py`, `app/run.sh` | AGPL-3.0-only; runtime-connected adapters, derived preparation/audio, retained tools |
| `vendor/froggytale/` | AGPL-3.0-only except components carrying their own vendor notices |
| `tools/audit.py`, `tools/release.py` | MPL-2.0; newly authored independent file-audit/archive tools, not linked into or copied from the runner |
| `include/native/gamepad-state.h` | MIT; independent native input transport, not the MPL-covered UI |
| `include/native/font8x8_basic.h` | Public-domain dedication retained from Daniel Hepper / Marcel Sondaar / IBM |
| `deps/SDL2/`, `deps/SDL2-build-headers/` | SDL2 zlib license and retained embedded third-party notices |
| `deps/gcc-12.3.0/libatomic/` | GPL-3.0-or-later with GCC Runtime Library Exception 3.1 |
| GCC configure/support files | Original per-file GPL/exception/permissive notices, not MPL |

LilSmoke adaptation and independent release tooling: José Pilas and contributors.
FroggyTale: Synaps33 and contributors. Butterscotch: mrpowergamerbr and its
contributors. SDL, GCC and all vendor authors retain their copyrights.

`LICENSE` and `LICENSES/AGPL-3.0-only.txt` reproduce the upstream AGPL text.
`LICENSES/MPL-2.0.txt` applies only to explicitly marked independent tools.
The runtime, game, project as a whole, and third-party vendors are NOT relicensed
to MPL. No separately licensed LilSmoke UI/driver source or binary is included.

Undertale, its characters, artwork, music and game data belong to their owners.
There is no authorization here to redistribute them. No Flowey icon, owner
file, proprietary atlas or other game asset is included. No warranty or
association with the game creators is offered.
