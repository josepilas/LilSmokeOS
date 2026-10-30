# TreeFrog Compatibility 0.2 Integration

## Build And Install

- Reproducible helper builder: `tools/treefrog-compat/build.py --target mips`.
- Final MIPS output: `_build/mips-20261029-compat02/`; `build.json` records
  source SHA-256, output SHA-256 and dynamic dependencies.
- New root UI: `_build/ui-root-20261029/payload/lilsmoke-ui`;
  `build-report.json` records the complete source inventory and MIPS ELF evidence.
- Runtime source: `package/lilsmoke-scripts/src/lilsmoke-treefrog-launch`.
  Install launcher, rebuilt `lilsmoke-treefrog-check`, and rebuilt
  `lilsmoke-treefrog-input` under `/usr/lib/lilsmoke`.
- Parent-owned production make integration: replace only the bridge link input
  `treefrog-common-input.cpp` with
  `$(BR2_EXTERNAL_LILSMOKE_PATH)/tools/treefrog-compat/profile-input.cpp`.
  That wrapper includes the existing common implementation unchanged and
  enables its extra digital-look mapping only for the ClassiCube profile.
  Keep the existing UI/script include directories and SDL2 linkage.
- Install the new UI for `/storage/treefrog-ports`; the preceding UI still
  discovers the old directory and is not compatible with this root migration.
- No changes to SDK, converter, independent frontend or SD media were made here.

## CLI

```
lilsmoke-treefrog-launch PORT_ID
lilsmoke-treefrog-launch --application /storage/independent-apps/RENAMED_FOLDER
lilsmoke-treefrog-check PORT_ID
lilsmoke-treefrog-check --application /storage/independent-apps/RENAMED_FOLDER
```

The application check prints three bounded tokens: `PORT_ID PROFILE LIBC_ABI`.
It returns 4 for bad metadata/data/path, 5 for incompatible engine ELF, 6 for
missing device glibc/driver and 7 for a missing direct-port PicoArch companion.
Converted Libretro uses its own frontend and does not require PicoArch.
The checker's `--application-exec` is an internal fixed `/app` entry inside the
owned legacy view. It constructs argv and execs the bundled host; it is never
shell evaluation of metadata. `--application-core APP_DIR` is a diagnostic
legacy-core-path output with the same preflight checks.

The adapter and runtime CLI are documented in the compatibility document and
`tools/treefrog-converter/runtime/INTEGRATION.md`. The latter frontend is owned
by the independent runtime work, not this component.

## Qualification

Host catalogue/UI tests: `test_catalog.sh`, `test_ui.sh` (ASan/UBSan).
Application boundary tests: `test_application.py` after a host helper build.
Pure input/loading tests also run under QEMU user mode with `-cpu 74Kf`.

Full VM command, run only in coordination with the parent:

```
python3 tools/treefrog-compat/test_runtime_vm.py --stock STOCK_CLOSURE --helpers tools/treefrog-compat/_build/mips-20261029-compat02 --out UNIQUE_OUTPUT --applications
```

The added application cases include the original native Hex-a-hop in a renamed
app folder and a small MIPS recording frontend that checks typed argv and data
containment. That recorder is NOT an independent Libretro game host. Its report
must never be used to claim actual independent-core, hardware or FPS validation.
Historical VM evidence uses the preceding `roms/ports` layout. The new fixture
stages originals under `treefrog-ports`; fixture-download paths stay historical.

Public firmware/driver redistribution and physical M8 performance remain
separate acceptance items. The parent deploys and migrates verified SD files.
