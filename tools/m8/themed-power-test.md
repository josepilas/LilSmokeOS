# Themed Power Off VM Regression

Run `python3 tools/m8/test_themed_power_vm.py` from WSL as root. Defaults:

- SDK: `/root/lilsmoke-os/build/buildroot/output` (never modified).
- Fixed libraries: `/root/lilsmoke-directfb-dispatch-20261021-qualified/payload/`.
- Baseline: `baseline-{libfusion,libdirect}.so` beside that payload directory.
- Original assets: `/root/lilsmoke-vm-validation/storage/themes`, mounted read-only
  over 9p, without copying the large theme trees.
- Font: SDK `target/usr/share/fonts/dejavu/DejaVuSans.ttf`; override with `--font`.

The runner builds one C++ harness from the listed production objects, excluding
`main.cpp`. It snapshots the small sources/headers into its disposable evidence
directory, and records commands, compiler version, dependency hashes and binary
hash in `sources-manifest.json`. A subsequent run can use `--binary PATH`; the
binary, harness and production sources must match the adjacent manifest. Production UI,
Settings, main, DirectFB patches and SDK target files are not edited by this test.

Each library pair gets a separate real MIPS Linux VM: Malta, 74Kf-lilsmoke, one
CPU, 120 MiB, Cirrus fbdev, actual SDL2/SDL2_image/SDL2_ttf/DirectFB DSOs. The SDK
SquashFS is in guest RAM; no block drive or physical SD is attached. Only the
disposable evidence share is writable. There is no host keyboard input. QMP is
used only for read-only status/register diagnostics after stalled progress;
`--debugger` optionally exposes a private guest GDB Unix socket.
The QA kernel command line uses its supported `nowait` option: a prior run
stopped at `r4k_wait_irqoff + 20` before Settings while the vCPU was halted and
guest timeouts stopped progressing. This disables only kernel CPU idle WAIT,
not SDL/DirectFB locks or thread cleanup. `--enable-guest-wait` retains the old
idle mode for diagnosis. The fixture verifies that `nowait` took effect and that
BusyBox timeout can terminate a disposable sleeping process before UI testing.

Default coverage is the default theme plus all four original themes, twice each.
The in-process button fixture renders SystemView, enters Settings, checks default
Cancel, A/right/left/A Cancel navigation, and B cancellation. Every cancellation
must leave Settings active, queue no launch and publish no helper marker. Then
A/right/A confirms Power Off. Like production main, the harness takes the launch
request and destroys views, cursor, Input, Font and Renderer before calling
`executeProcessLaunch`. SDL must be uninitialized and only one task may remain.

The unchanged real `lilsmoke-power poweroff` runs with
`LILSMOKE_STOCK_SUPERVISOR=1`. Successful replacement must exit 0 and publish
exactly `poweroff\n` at `/run/lilsmoke-poweroff-request`. Additional cases check
helper refusal (75, no marker), failed exec (1, no marker), and successful fresh
UI initialization after those failures. Each child has a 45s timeout plus 5s kill
grace; each VM has a 600s host deadline. These bounds are configurable but capped.

`report.json`, per-case logs, guest library hashes, read-only mount evidence,
nonblank pixel hashes, BMP captures, and VM commands are retained under the
printed `/root/lilsmoke-themed-power-*` evidence directory. The qualified run
must pass. Baseline results are observational: an original-library race need not
reproduce in this bounded UI flow; `old_failure_reproduced` is true only when a
case actually fails after taking the launch and before completing UI destruction.

Reports include canonical source fingerprints and per-file drift at both ends of
the run. Source drift makes `current_sources_qualified` false and the default
command fail, even when the frozen snapshot passes its runtime checks. Explicit
`--allow-source-drift` permits intentional testing of an older snapshot, without
qualifying current sources. The `qualified` variant is the payload-directory
label, not a claim that broader integration or RetroArch teardown is qualified.

This tests the UI-to-shutdown-request path, not physical power removal, the SD
unmount guard, HC framebuffer MMIO, cycle-accurate frame timing, or smoothness.

## Separate Open Issue

The 2026-10-21 RetroArch fixtures `frontend-op2yb1nb` (patched pair) and
`frontend-f1_rg2tk` (baseline), under `/root/lilsmoke-native-duplicates-20261010`,
both completed 1000 game frames/menu restoration but exited 139 during teardown.
Both serial logs record an invalid write at libc offset `0x8fe3c`. The themed UI
passing does not resolve or qualify that separate lifetime/ABI investigation.
No libc workaround, RA change, or speculative DirectFB patch belongs in this test.
