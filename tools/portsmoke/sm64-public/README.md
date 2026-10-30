# SM64 Public BYO-Data Work

This directory is the asset-free release implementation, not the private
asset-bearing 0.6 executable and not the older recreated gameplay demo.

The public application must build its ROM-dependent objects in its own cache
from a supported ROM placed in `original/`. The filename does not identify the
ROM; normalized contents and the pinned US 1.0 SHA-1 do. `.z64`, `.v64` and
`.n64` byte ordering are accepted. A Downloads/source copy is never deleted.

The existing full game's graphics implementation permits source distribution
but not binary redistribution. It may therefore be included as retained,
licensed source and compiled locally by the end user. A native compiler, if
bundled, must generate musl-compatible game objects and include its own notices
and corresponding-source materials. It must not be a PC cross compiler falsely
labelled as a device runtime.

First-run work is serial and must pass within the real MIPS memory budget.
Extraction/cooking progress, cancellation, cache validation, atomic completion,
restart without the ROM and actual original-engine gameplay are release tests.
Delete the app's `original/` copy only after a durable verified cache is ready.

The published Pre-Alpha-0.0.1 package has a separate corresponding-source asset.
Local first-run fixes are being validated independently; an SD patch does not
update that GitHub release or its source offer automatically. Do not substitute
a private executable or distribute the locally cooked game/cache.

## First-Run Regression Tests

### No-RTC FAT32 Fix

Local `0.0.1-prep.7` corrects a physical M8 failure that the previous correctly
dated VMs did not reproduce. The vendor system can run at Unix epoch 1970;
FAT-loaded inputs are dated at least 1980. GNU Make repeatedly regenerated an
included rules file with an earlier in-memory timestamp, restarting until the
planning timeout at 50%. Increasing that timeout does not fix the loop.

The native `sm64-build-shell` timestamps successful private build targets with
a logical epoch newer than verified source inputs. It leaves system/host clocks
unchanged and retains dependency checks. The wrapper preserves command failures,
rejects linked/unsafe outputs, accepts multi-command recipes before their output
exists, and supports the legacy o32 utime ABI if newer timestamp calls are absent.
Its time_t/kernel-long distinction is covered by forced-ENOSYS host/MIPS tests.
The loading UI no longer redraws an identical bitmap continuously while building.

`clock_vm_test.py` is the dedicated preparation/game harness with one CPU,
120 MiB guest RAM, a 128 MiB virtual-address limit, a FAT32 block image and **1970 guest
time**. It tests extraction/cancel/resume, compilation, verified cache commit,
game/menu/return, repeated launch and FAT readback. It changes only the private
VM clock, never Windows or WSL time. A fresh full VM build is supported. For
practical whole-system test time, `--resume-snapshot` explicitly combines a
separate fresh-clock extraction/planning checkpoint, independently completed
full native-MIPS build, and a second FAT/1970 VM resume test. The private
snapshot's ELF must hash-match that committed native build; renderer, sound
data, large castle/animation/actor data and complex behavior/camera C units
are recompiled and the game relinked inside the RAM-limited VM before
render/menu/cache checks. The report identifies this as split testing, never
as full fresh VM compilation. `export_clock_update.py` validates all those
gates before export. A partial run alone is not acceptance. Game data/cooked
binaries remain private QA artifacts and are never distribution inputs.

`--previous-vm` permits one narrower qualification path from prep.5. It requires
the exact assembler `-nostdinc` delta and uncommitted-game cleanup in cook.py,
otherwise identical runtime files, a cleanly unmounted predecessor
FAT image, the known header error, and six completed C jobs without OOM.
Each VM-produced object must match the newly completed independent native
snapshot byte for byte. Only then may the new VM reuse those six objects and
run the audio assembler, final link and game checks. Reports and export gates
separate the previous C jobs from the current jobs; the failed predecessor
preparation is never reported as successful. This path cannot accept changed
C flags or arbitrary code changes, and supplies no precompiled game to users.

Resume also handles a power cut after copying the final executable but before
cache commit. A bounded, regular, unshared working/game is discarded only after
saved source verification; the game is then validated and copied again. A
committed ready generation is never treated this way. Linked pending outputs
fail closed without changing their targets or deleting the ROM. The final VM
deliberately starts with a completed but uncommitted snapshot copy and must
prove its cleanup, cancellation and successful later commit.

Under heavy compilation, the Malta fixture's SquashFS root can strand file
faults during reclaim (also observed with IDE, not only virtio/9p). The optional
`--uncompressed-root` uses the unchanged root files on a private, read-only
ext2 block image. Physical RAM, process limits, FAT32 app disk, CPU and clock
remain unchanged; it is not a root-in-RAM workaround. Reports identify this
root format. Incomplete compressed-root runs are excluded from acceptance.

`tools/m8/install_sm64_clock_fix.ps1` clean-installs the accepted package on the
identity-verified spare card only. It verifies all files before activation,
preserves saves and the old app outside the catalog, supplies the owner's ROM
in `original/`, and places the same asset-free package in `packages/`. It never
formats, repairs or repartitions; `-VerifyOnly` performs a read-only preflight.

The earlier 90 MiB virtual-address guard was too restrictive, but raising it
alone did not solve physical memory exhaustion. A cleanly closed VM retained
the actual failure: GCC was killed while parsing `sound/sound_data.c`, after
the renderer had completed. The original Makefile hid its separate syntax
check, leaving the progress display on the preceding renderer task.

`sound_data.S` now embeds the four locally extracted/generated audio blobs
with `.incbin`, instead of asking GCC to parse megabytes of C byte tokens.
Private original-data and asset-free synthetic tests verify the exact data
bytes and all four global symbol offsets, sizes and bindings against the C
object. No audio assets are shipped. The private Make fragment also compiles
C and generates its dependencies in one pass, without a duplicate syntax pass.
It does not edit verified source bytes or weaken dependency checks.

The audio assembler preprocessing explicitly uses `-nostdinc`: its only
directives are builtin target checks and .incbin, so it must not try to load
a glibc host's implicit stdc-predef.h on the musl device. Sound equivalence
tests use the same no-system-header mode.

Preparation permits 128 MiB of virtual address space for executable/library
mappings. The VM still has 120 MiB physical RAM (~110 MiB Linux usable), one
CPU and no swap; the game keeps its separate 64 MiB virtual guard. Renderer
optimization flags and game audio bytes are unchanged. Acceptance must finish
the actual renderer/audio jobs and link without kernel OOM, then run the game.

Large graphics data also made GCC retain tens of MiB of macro diagnostic
locations. Preparation uses `-ftrack-macro-expansion=0`, retaining existing
optimization flags. Native-MIPS profiling produced the byte-identical castle
object with sampled user-mode RSS falling from 103,092 to 33,064 KiB. This is
not physical-RAM acceptance; whole-system tests must still pass. The option
changes diagnostic macro histories, not rendering, as described in
[GCC's documentation](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html).

`test_device_resume_pipeline.py --snapshot <private-directory>` optionally
retains the completed build before cache pruning. This synchronous QA hook
does not modify production modules; the captured ELF must match the final
committed native game's hash. Such snapshots contain private game data and
must never be packaged or published.

### Preparation And Recovery

Local `0.0.1-prep.2` separates ROM extraction from native engine preparation.
For the supported US ROM, extracted assets total 10,679,837 bytes (10.68 MB).
That counter stays constant while the engine compiles: it is not a measure of
compiled objects or source-archive bytes. The previous counter reset and
unreported texture processing could make valid work look frozen at 50%.

The UI now shows the current texture/compile/assembly/audio/generation/link
task and elapsed time. Planned serial recipe counts drive the percentage;
elapsed time never fabricates completed work. Full preparation can still take
a long time on the device. B cancels safely; relaunching checks the saved
inputs and resumes verified work. The last interrupted output is regenerated.
Unknown multi-output producers conservatively rebuild generated outputs,
without discarding the verified extraction. A committed game starts without
recompiling. Only after durable game verification is the app's original removed.

Checkpoints bind source/asset hashes to the exact build recipe and supported
ROM. They are private local state, not a source for package generation. Legacy
work must match a fresh trusted extraction before migration; merely finding
files at the expected paths is not enough. The buffered `sm64-bin2c` helper
emits equivalent byte values with smaller decimal C tokens. Compiler garbage
collection uses a smaller initial heap, and render preflight uses SDL's dummy
video backend so the extraction UI retains ownership of the physical display.

`native_build.py` unpacks the checksum-verified engine source with bounded,
cancellable buffered I/O and actual byte progress. It moves the normalized ROM
instead of copying it twice. Verified MIPS asset helpers are retained regardless
of scratch/FAT timestamps; a guarded private Makefile patch disables only the
known parse-time helper rebuild, not the real game compilation.

```sh
cd tools/portsmoke/sm64-public
python3 -m unittest test_cook test_native_build test_resume test_build_progress \
  test_package test_split_sources test_preparation_export test_progress_audit
bash test_preparation_mips.sh /root/new-private-sm64-preparation-test
```

The full preparation test uses a disposable copy of the owner's ROM and a fresh
cache. It compiles the engine natively, checks 30 rendered preflight frames,
verifies durable commit and second launch, and deletes only the committed app's
test original. The owner's Downloads ROM stays untouched. All generated games,
private caches and ROM-bearing FAT QA images must remain outside release files.

`extraction_vm_test.py` exercises the real SDL screen and source/asset extraction
on a disposable FAT32 disk with one CPU, 120 MiB guest RAM and a 90 MiB process
limit. It extracts, cancels, relaunches without re-extraction, reaches actual
compilation, then cancels again and proves the original remains. Its FAT32
image is unmounted cleanly. `test_device_resume_pipeline.py` separately compares
legacy source/assets against a fresh extraction, interrupts texture work,
resumes the full native-MIPS build and checks render preflight/commit/relaunch.
`audit_completed_progress.py` reconciles its real final build with the dry run.
The known included `level_rules.mk` CPP producer runs during GNU Make planning;
the live total is conservative by one task. The audit records the raw counts
and separately reconciles pending work, without inventing a live completion.
`export_preparation_update.py` refuses deployment export without full native,
progress-reconciliation, real-UI/resume and production-installer acceptance.
`game_vm_test.py` exercises home -> Applications -> prepared game -> menu -> return
using a separately prepared private cache. Neither test measures physical M8 FPS
or proves that every first-run compiler job fits the physical vendor kernel.
