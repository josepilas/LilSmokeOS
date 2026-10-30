# LilSmoke Port Converter 0.2.0

This tool is for ports that **LilSmokeOS native TreeFrog compatibility cannot
handle**. Working TreeFrog ports should instead be copied, unchanged, as the
`.sh` plus game folder into `LILROMS/treefrog-ports` (`/storage/treefrog-ports`).
That direct compatibility path is separate from LilSmoke Applications.

## What Is Implemented

The browser sends the selected ZIP to a local Web Worker, not a server. The
conversion engine is C compiled to WebAssembly; JavaScript does not reimplement
ZIP conversion. Pinned miniz and cJSON decode the input. The C pipeline:

1. Identifies TreeFrog metadata/layout, validates files, required data sets,
   archive integrity, portable paths, quotas and collisions.
2. Inspects the actual ELF loader view, MIPS/o32/FPU/endianness, libc family,
   dynamic dependencies and callable Libretro exports.
3. Searches compatible same-family engine alternatives in the input, resolves
   transitive dependency providers and chooses an independent frontend ABI.
4. Preserves native shell setup, loops, environment, helpers, sourced settings
   and runtime choices instead of matching game-specific launcher templates.
   Native scripts execute as trusted programs in the device compatibility
   namespace, with BusyBox syntax checked before launch. The independent
   Libretro route still rejects setup that would otherwise be silently skipped.
5. Generates a typed launch plan, software video/audio/input/save/probe policy,
   metadata and one arbitrarily named root shell launcher. Selected libraries
   are normalized to the installed runtime search path. Original engine/data
   bytes and notices are preserved; metadata changed by an alternate-engine
   route is retained separately in its original form.
6. Bundles the independent `lilretro` frontend, its source/notices and selected
   reviewed dependencies. First launch probes the real core's API capabilities.

The result is a ZIP-format `.lilpck` with root `meta.json`, `adapter.json`, one
root `.sh`, and `runtime/`. Install through LilSmoke Explorer. Original scripts
can remain inside subdirectories, but the converted Libretro launch does not
execute an original shell script or the old PicoArch frontend.

## Strategies And Honest Limits

Metadata is preferred, but is no longer mandatory for an unambiguous native
ELF/launcher pair. Such ports get inferred canonical metadata and a wrapper
which preserves the arbitrary original script name. Referenced helper ELF
architecture, libc and dependency closure are checked alongside the main
program. Invalid existing metadata is never bypassed by inference.

The planner tries its available routes, not just the first engine filename.
The report records declared-engine analysis, same-family alternatives, runtime
ABI/providers, and detected build scripts. A failed automatic route is **not
proof that a manual/source adaptation is impossible**. Unknown source-build
recipes are reported for a matching MIPS SDK; the browser does not compile
arbitrary engines, translate CPU instructions, decompile games, or replace a
GPU-only renderer with a newly written software engine.

The independent frontend is original MPL-2.0 C code. It supports software
Libretro video, PCM, input, bounded options and capability diagnostics. Unsupported
interfaces are refused, not falsely advertised. A dynamic probe executes trusted
native code and is not a security sandbox or proof of performance.

The supplied runtime pack contains glibc and musl frontends, **not a universal
fully self-contained libc/SDL distribution**. Device-specific glibc/SDL and
hardware-driver closure still comes from the matching LilSmoke platform import.
Unreviewed SDK/firmware libraries are not published. The tool labels those
dependencies explicitly. Never load a glibc core in a musl host. Only reviewed
libraries can be added to the runtime pack; all upstream licenses remain in force.

Input ZIPs are capped at 384 MiB, expanded contents at 768 MiB, and WASM memory
at 1,536 MiB. These are desktop conversion limits, not device RAM promises.
The target has no 3D GPU. Device testing remains necessary for controls,
rendering, sound and speed. Conversion does not grant redistribution rights to
game data: downloaded packages containing personal assets stay private.

## Build And Test

Standalone publication has two branch roots: `source` contains this complete
project; `dist` contains the static Pages site. Author: José Pilas. First-party
converter and independent lilretro sources are MPL-2.0; see `LICENSE` and
`THIRD-PARTY-NOTICES.md`. No OS checkout is required for the converter build.

On Ubuntu 24.04 or WSL Ubuntu 24.04, from the source branch root:

```sh
sudo apt-get update
sudo apt-get install -y build-essential python3 emscripten nodejs npm pkg-config libsdl2-dev llvm-15
# The distributed WASM was built with Emscripten 3.1.6.
emcc --version
python3 -B build.py --wasm --sanitize
python3 -B tests/test_core.py
python3 -B tests/security_qa.py --wasm --audit --brief
python3 -B tests/test_publication.py
python3 -B runtime/build.py --target host
python3 -B runtime/tests/test_runtime.py --build runtime/_build/host
python3 -B runtime/tests/test_hotkey_runtime.py --build runtime/_build/host
```

The vendored miniz/cJSON sources are included and pinned. `build.py` only fetches
missing pinned dependency files; it never fetches games or an SDK. `--out DIR`
and `--dist DIR` can isolate generated outputs. Python 3.9+ is required. Native
and Node/WASM synthetic tests do not execute game code.

### Browser Checks

Use Node 20+ for Playwright (the Node/WASM synthetic tests also work with Node
18). Install Node 20+ using your usual distribution/version manager first.

```sh
npm install --ignore-scripts
npx playwright install chromium
# Copy only the PUBLIC runtime ZIP into site/dist/runtime/runtime.zip.
# In one terminal, serve just the site under the exact Pages project prefix:
python3 -B tests/serve.py --directory site/dist --port 8777
# In another terminal (input remains private and outside the served tree):
node tests/browser.mjs --base-url http://127.0.0.1:8777/LilSmoke-Port-Converter/ \
  --input "$TEST_ZIP" --expect-download "$EXPECTED_DOWNLOAD"
```

`TF_TEST_ZIP`, `TF_BASE_URL`, `TF_BROWSER_EXECUTABLE` and
`TF_PLAYWRIGHT_MODULE` are environment alternatives. The dependency also resolves
normally through Node, including `NODE_PATH`. `--out` stays under `_build/`.
Browser reports, screenshots and downloaded packages are private build outputs,
never publication inputs. `ui-tests/` contains additional Windows synthetic UI
checks; its output also goes to `_build/`. Host-specific full-system VM QA and
its pad helper are intentionally excluded: they require a separate OS tree.

### Independent Runtime

The local `runtime/gamepad-state.h` preserves the original MIT transport header.
The independent frontend requires SDL2 on a host/musl build or matching SDL1 on
the MIPS glibc target. Host builds do not need a MIPS SDK. Device builds require
external, matching MIPS32r2/o32/FP32 hard-float SDKs; none is included:

```sh
python3 -B runtime/build.py --target musl --sdk "$MUSL_BUILDROOT_OUTPUT"
python3 -B runtime/build.py --target glibc --sdk "$GLIBC_SYSROOT" --cc "$MIPS_GCC"
python3 -B runtime/pack.py --require-both \
  --musl-sysroot "$MUSL_SYSROOT" --glibc-sysroot "$GLIBC_SYSROOT"
```

The optional pinned SDK acquisition script is a build tool, not an included SDK
or a redistribution license. Device libc/SDL/driver libraries stay external.
`runtime/INTEGRATION.md` describes the supported CLI and device limitations.
Do not load glibc cores or drivers into a musl process. There is no hardware
performance or gameplay guarantee, and no game-specific adaptation recipe.

### Publication And Deployment

```sh
# Requires built site/dist assets plus its runtime/runtime.zip; uses local licenses.
python3 -B source_release.py --elf-tool llvm-strip-15
```

This offline command writes `_build/github-publication/{source,dist}` and
deterministic source/dist ZIPs with fixed ZIP times, modes and sorted entries.
It refuses nonempty outputs. `publication.json` inventories every file by size
and SHA256 and records a recursive content audit. The source ZIP contains all
first-party runtime tests/build tools, not only the converter C files.

Publication runtime copies remove nonloadable debug/file symbols from the
frozen MIPS ELFs; executable load segments are compared, excluding ELF header
section-table bookkeeping. Original artifacts are never overwritten. Embedded
build records use `${PROJECT_ROOT}` / `${SDK_ROOT}` placeholders and retain
original input/binary hashes; the pack manifest hashes the published contents.

In the standalone `josepilas/LilSmoke-Port-Converter` repository, the parent can
commit the **contents** of the `source/` staging directory to branch `source`
(default branch), and the **contents** of `dist/` to branch `dist`. Neither
staging tree contains Git metadata. Configure GitHub Pages: Deploy from a
branch, branch `dist`, folder `/ (root)`. Keep `.nojekyll`. Assets and the source
download use relative URLs under `/LilSmoke-Port-Converter/`; no root rewrite,
custom hostname, Sites identity or hosting credential is needed. The root ZIPs
are suitable parent-managed `0.2.0` release attachments. This tooling does not
create branches, push, tag, configure Pages, or deploy.

Licenses do not grant rights to game assets, firmware or SDK libraries. Test
results describe static conversion, API behavior and browser packaging only.
