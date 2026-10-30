# Independently authored TreeFrog catalogue

This catalogue consumes the requested release metadata contract. No TreeFrog
frontend source, runtime script, release folder, or package is copied or modified.
The UI uses the existing unthemed ListView and ProcessLaunch handoff, not the ROM
scanner, emulator registry, favourites, or recent-game launcher.

## Standalone preflight API

Header: `package/lilsmoke-ui/src/src/TreeFrogCatalog.h` (namespace `lilsmoke`).
Compile `TreeFrogCatalog.cpp` with C++17, the UI source include directory,
`package/lilsmoke-scripts/src`, and `pkg-config --cflags --libs json-c`.
No SDL dependency is needed for this translation unit.

```cpp
std::string error;
auto metadata = TreeFrogCatalog::parse(jsonBytes, folderId, &error);
auto installed = TreeFrogCatalog::find(folderId, "/storage/treefrog-ports", &error);
auto catalogue = TreeFrogCatalog::scan();
bool idOK = TreeFrogCatalog::validId(folderId);
bool pathOK = TreeFrogCatalog::validRelativePath(relativePath);
```

`parse` is pure schema validation, capped at 64 KiB. `find` reads the original
config and checks original files with descriptor-relative, no-symlink walks.
Both return `std::optional<TreeFrogPort>` and optionally a rejection diagnostic.
Missing files do not invalidate otherwise valid metadata: `find` returns the port
with `available() == false` and `reason` suitable for the UI.

The returned port exposes `launchType` (`native` or `libretro`), `binary`, `args`,
`content`, `requiredFileSets`, `missingFileSets`, `missingData`, `directory`,
`boxartPath`, `hasRunScript`, `hasBinary`, `hasContent`, `requiredFilesSatisfied`,
and `filesChecked`. Paths in launch/required fields remain port-relative, including
an optional leading `./`. `missingFileSets` has the same order as the alternatives
in `requiredFileSets`, retaining only missing paths in each. An empty inner list
means that alternative is complete. All files in one alternative are required;
any complete alternative suffices. An empty outer list requires no extra data.
`hasContent` is true when no content field is specified. `parse` alone leaves
`filesChecked` false and cannot report launch availability.

IDs are 1-64 ASCII letters/digits/underscore/hyphen, starting with a letter or
digit, and must match the folder. Names are 1-256 UTF-8 bytes. Launch/required paths
are 1-1024 bytes, relative, without traversal, backslash, colon or control bytes.
No symlinks are followed in the port folder, metadata, artwork, or checked files.
Arguments are strings (including empty strings), at most 1024 bytes each and 128
entries. There are at most 128 alternatives and 128 required paths in total.
Optional `missingdata` is literal text up to 2048 bytes; it is never interpreted.
Unknown metadata is ignored within the overall JSON size/depth budget.
Scans retain at most 128 valid ports and inspect at most 4096 directory entries.

The UI refreshes on each entry and revalidates the selected ID before requesting
`/usr/lib/lilsmoke/lilsmoke-treefrog-launch <id>`. Only this ID enters the helper's
argv. Runtime policy and executing the original `run.sh` belong to that helper.
The helper must revalidate independently at launch time; UI checks are not an
authorization boundary. It must not use raw metadata as shell commands.

## Host verification

Run `sh tools/treefrog-compat/test_catalog.sh` on Linux/WSL with g++, json-c and
pkg-config installed. Address/undefined sanitizers are enabled by default; set
`SANITIZE=0` to disable them. All generated fixtures live in temporary directories.
The JSON fixtures here are synthetic, independently authored test data.

Run `sh tools/treefrog-compat/test_ui.sh` for the headless ListView integration
checks and 640x480/320x240 nonblank frame captures. Set `BUILD_UI=1` to also build
the complete UI/session using the existing Makefile in an isolated temporary copy.
The launch helper is mocked for UI tests; no release script or runtime executes.
The same runner also checks the production ProcessLaunch direct and M8-wrapper
argument vectors with exec mocked, including absent-wrapper and no-double-wrap
cases. This host check starts neither a loading logo nor a foreign engine.

## MIPS Payload And Actual UI VM

`build_ui_mips.py --out <new-output-directory>` uses the existing
`tools/ui/build_isolated.sh` and current SDK. It hashes build inputs before/after,
retains stripped UI/session payloads plus their SHA-256 and ELF metadata, and removes
temporary objects. It neither copies nor installs/rebuilds the SDK.
The optional `--baseline <previous-output-directory>` compares the stripped UI
byte-for-byte by SHA-256 and lists changed audited source inputs. Existing actual
UI VM evidence is referenced only when its tested payload matches the new payload
exactly; that reuse is explicitly recorded as not being a new VM run. Uncompiled
external runtime `.cpp` files are excluded from the UI source audit.

`test_actual_ui_vm.py --binary <stripped-ui> --out <new-report-directory>` reuses
the SDK's existing squashfs read-only, overlays the production UI and its current
library closure, and boots the existing one-CPU MIPS Malta frame harness at 128 MiB
with no swap. The original extracted Hex-a-hop ports directory is a read-only,
no-exec 9p mount, not an SD image or repacked release. Host/guest original-file
hashes are checked before/after.

QMP keyboard events navigate actual Home -> TreeFrog Ports -> original Hex-a-hop,
return and re-enter to verify rescanning, then select it. A recording helper exists
only in this guest overlay at the real launcher path. It records the sole ID
argument and process identity without calling any engine or original `run.sh`.
The check requires replacement of the UI process, closed UI device descriptors
and removed UI mappings. A guest-only supervisor restarts the actual UI after the
recording helper exits; Home must render identically, input must reopen the
catalogue, and a second selection must complete another clean handoff.

The current production `lilsmoke-treefrog-input --engine` is exercised against a
guest-only script in a minimal bind-mounted mock root, not an actual engine. A
token-only mock logo acknowledges removal of `/run/m8-loading/active`; the bridge
must wait for that acknowledgement and remove the fixed boot-state token before
the script starts. Both cycles check removed loading environment, no active logo,
and successful acknowledgement. This does not test physical M8 scanout or run the
generic loading wrapper in the VM; its argv routing is a separate host unit gate.

Initial and catalogue UI VmHWM and guest memory snapshots across both cycles are
retained. Available memory must remain above 16 MiB, with no swap or OOM. These
samples are not a continuous peak profile or a long-duration leak test.

Clean exits, nonblank frame captures and unchanged original releases are required. Reports and
PNGs explicitly qualify only the UI and handoff, never native/libretro gameplay,
physical scanout, or production return-supervisor integration. Temporary initramfs/PPM files are
removed; the SDK/rootfs are never copied or modified.

## Final Qualification 2026-10-28

Final MPL-2.0-tagged sources were built with the existing SDK in a disposable
13,165,476-byte isolated build. Retained objects were unavailable. No SDK install,
global rebuild, rootfs copy, runtime edit, Input edit, or SD operation was used.
All 105 recorded UI/build input hashes still matched the current tree on final
verification. The parent Input detach method is included in this source audit.

- Build: `_build/ui-mips-20261027-03/build-report.json`, passed.
- UI: `_build/ui-mips-20261027-03/payload/lilsmoke-ui`, 1,054,012 bytes,
  SHA-256 `79c61b892b45c81308ed14c5b1a016a21d416cfea949dc06526eb773ace93341`.
- Session: `_build/ui-mips-20261027-03/payload/lilsmoke-session`, 67,492 bytes,
  SHA-256 `4f5f00153d41876cd326b21d7843353eec330f5717ab7a8bd27f344fd075ec4b`.
- Both payloads are byte-identical to the earlier qualified payloads. UI ELF is
  little-endian MIPS32r2, o32, PIE, stripped; GCC 12.3.0, C++17, `-O3 -flto`.
- Host catalogue ASan/UBSan: passed schema, size/depth/path/ID budgets, alternative
  data sets, native/libretro fields, no-symlink checks, FIFO rejection, bounded
  enumeration, fresh scans, and original-script preservation.
- Host UI ASan/UBSan: passed 640x480 and 320x240 valid/missing/invalid/empty states,
  selection revalidation and ID-only handoff. The same test runner's production
  ProcessLaunch direct/M8-wrapper argv checks also passed with exec mocked.
- Fresh actual UI VM: `_build/ui-mips-20261027-03/actual-ui-vm/report.json`, passed
  Home -> TreeFrog Ports -> original Hex-a-hop, back/re-entry/rescan, ID-only exec,
  closed device descriptors and replaced UI mappings, first clean exit, identical
  Home after restart, responsive catalogue after return, and second clean exit.
- The VM used the parent's production bridge SHA-256
  `f5f2d8de5416c536d2861416bf27c1243ebb9e2263814b776c1ea4a2c7002668`.
  Both token-only M8 mock-logo cycles acknowledged release before mock engine
  script entry, with active/state tokens gone and loading environment removed.
- One virtual CPU, 128 MiB RAM, no swap. Catalogue VmHWM: 21,132 KiB; returned
  initial-frame VmHWM: 21,052 KiB. Minimum sampled MemAvailable: 91,472 KiB.
  AnonPages returned from 7,224 KiB in the catalogue to the 88 KiB initial baseline
  after both exits. No OOM/segfault; all original file hashes unchanged.

Seven PNGs and raw argv/PID/FD/maps/memory evidence are alongside the VM report.
The guest was shut down and the temporary initramfs removed. This run deliberately
does not qualify gameplay, physical M8 logo/scanout, real loading-wrapper display
behavior, production supervisor return integration, or long-duration leak freedom.
