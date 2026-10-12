# Third-Party Notices

Created and led by José Pilas. Original OS material designated for the initial
release uses a personal-use proprietary license; public SM64 authored code
uses MPL-2.0. Existing grants are not revoked. The
following projects, embedded runtimes and upstream modifications keep their
own terms. This summary is not a substitute for the exact licenses in source
and Buildroot `legal-info`. Verify versions and compiled options per artifact.

| Component | Retained Terms / Release Note |
| --- | --- |
| Buildroot and Linux | GPL-family; retain exact notices and matching corresponding source, including BSP modifications |
| BusyBox | GPL-2.0; matching build/configuration/source required |
| musl | MIT |
| SDL2 / SDL1.2 compatibility dependency | zlib; project-authored bridge files are separately MPL-2.0 |
| DirectFB | LGPL-family; exact modules and linkage require audit |
| RetroArch / libretro-common / core-info | GPL-family / per-file notices; not a single universal core license |
| PCSX4ALL | GPL-family and reconstruction provenance; retain upstream notices and matching modified source |
| PCSX ReARMed / Lightrec / GNU lightning / Unai | Individual GPL/LGPL/per-file terms; MIPS build does not replace them with MPL |
| OpenDingux emulators and engines | Per-project licenses; see pinned sources and legal-info, not the application-tab label |
| Snes9x/PocketSNES, Genesis Plus GX, FBNeo/MAME-derived cores | Custom/restrictive license terms may apply; explicit review is required, including commercial-use restrictions |
| mGBA, Gambatte, FCEUmm, Mednafen and other cores | Retain each core's exact upstream terms |
| FFmpeg | This recipe selects LGPL configuration; decoder/subcomponent notices still apply |
| libarchive, 7-Zip/LZMA, zstd, libwebp, libpng, JPEG libraries | Per-library BSD/LGPL/GPL/public-domain or other retained terms; use actual linked package notices |
| FreeType and DejaVu | Font/library licenses are separate from UI ownership |
| Dropbear, wpa_supplicant, BlueZ, ALSA and libusb | Retain exact MIT/BSD/GPL/LGPL terms per component |
| libini | LGPL-2.1-or-later; retained in its vendor directory |
| TinyGL, bitmap/offset metadata, compression routines | Retain the source-specific notices; old prototype components are not the original SM64 engine |
| Python, Tcl/Tk, PyInstaller, OpenSSL and zlib-ng | Desktop Device Adapter runtime notices are bundled under its `third_party` directory |
| Lucide | ISC; icon notices remain in their asset folders |
| n64decomp/sm64 and the pinned SM64 software-port fork | No blanket MPL grant for Nintendo-derived source/assets; full local package is private |
| Emill/MaikelChan graphics code | Source-only notice explicitly prohibits binary redistribution; public SM64 remains blocked |
| HCSEMI/CubeGM stock boot files, kernel, AVP firmware and vendor libraries | Owner-local import only until exact redistribution rights and source obligations are satisfied |

Keep all third-party copyright lines, license files and attribution. Do not
include ROMs, BIOS/game asset collections, private exports, stock card images,
proprietary theme art or Nintendo-derived icons in a GitHub source/release
bundle merely because they exist in the development workspace.

Research/inspiration credits are in `CREDITS.md`; inspiration is not a claim
that another project endorses LilSmokeOS or has transferred its trademarks.
