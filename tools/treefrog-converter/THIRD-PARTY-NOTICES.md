# Third-Party Notices

First-party converter C, web UI, independent lilretro frontend and build/test
tools: Copyright (c) 2026 José Pilas, Mozilla Public License 2.0 (LICENSE).
The independent frontend is original code, not a PicoArch implementation copy.

| Component | Terms And Included Notice | Provenance |
| --- | --- | --- |
| miniz | MIT, vendor/miniz/LICENSE and licenses/miniz.txt | richgel999/miniz, pinned in build.py and vendor/manifest.json |
| cJSON | MIT, vendor/cjson/LICENSE and licenses/cjson.txt | DaveGamble/cJSON, pinned in build.py and vendor/manifest.json |
| Libretro API header | MIT notice in runtime/vendor/libretro.h | libretro/libretro-common, runtime/vendor/sources.json |
| jsmn | MIT, runtime/vendor/jsmn-LICENSE | zserge/jsmn, runtime/vendor/sources.json |
| gamepad-state.h | MIT transport header, Copyright (c) 2026 José Pilas | Unmodified local runtime/gamepad-state.h from LilSmoke UI; runtime/vendor/gamepad-MIT.txt |
| Lucide subset | ISC, full notice in site/dist/icons.js (icons.js in dist) | Lucide Icons and Contributors |
| Feather-derived icons | MIT, full notice in icons.js | Cole Bemis |
| Emscripten generated glue/support | MIT/NCSA and bundled notices, licenses/emscripten.txt | Emscripten 3.1.6, emscripten-core/emscripten tag 3.1.6/LICENSE |
| WASM musl support | licenses/wasm-musl.txt | Emscripten 3.1.6 system/lib/libc/musl/COPYRIGHT |
| compiler-rt support | licenses/compiler-rt.txt | llvm-project llvmorg-14.0.0/compiler-rt/LICENSE.TXT |

Playwright is an external development dependency (Apache-2.0), not bundled in
the static site. Host/MIPS SDL, libc, compiler SDKs and firmware drivers are
external dependencies, not redistributed here. Their licenses and corresponding
source obligations remain separate; a mirror's top-level license does not
relicense GNU/SDL libraries. No game code, game data or user archives are part
of this publication. Component copyright notices in source remain authoritative.
