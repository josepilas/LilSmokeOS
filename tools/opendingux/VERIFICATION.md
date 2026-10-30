# Verification Status (2026-10-08)

- Production package: parent Buildroot lilsmoke-opendingux build/install
  completed successfully against native MIPS/musl target and private
  bounded libsquashfs 1.2.0. No test-root flags are in production.
- Host fixture suite: 19 tests passed, 2.183 seconds. Real gzip/XZ OPKs,
  no scan extraction, streaming data/cache validation, traversal, symlink,
  FAT collisions, depth/block/corruption bounds, uClibc/glibc/GPU rejection,
  native desktop policy, malformed metadata, argv/ROM/catalog limits,
  512 MiB budget, mkdir lock ownership/staleness and extra cache content.
- MIPS/QEMU 74Kf fixture suite: same 19 tests passed, 3.419 seconds.
  Linked against production bounded libsquashfs/libsodium/libini.
  Log: /root/lilsmoke-opendingux-qa/mips-tests.log.
- SHA256/desktop/argv/path/ELF unit checks passed on host, ASan+UBSan, and
  MIPS/musl/QEMU 74Kf. Target FP32/hard-float and DSP1/DSP2 accepted.
- Read-only actual installed production target scan passed under QEMU
  with the documented AS-limit shim. PocketSNES record:

    od-b4f617046bce9188181455925915bbe3b02e2ba87f238e6bf8570eef67fa3cf1<TAB>PocketSNES<TAB>snes<TAB><TAB>1

- Native SDL2 QA executable passes metadata/ELF scan. Disposable original
  native-qa.opk built at /root/lilsmoke-opendingux-qa/fixture/native-qa.opk.
  Final exclusive-fullscreen artifact SHA256:
  5446fec66ff130a4f67fd4fe70aad0cec6bb46071acb061b5d5f474aff90eeed.
  Rebuilding changes the SquashFS timestamp and thus its content hash.
  VM catalog ID when copied as /storage/opendingux/apps/native-qa.opk:
  od-19ca4170304e487dcd41b2e3c8be14968caca7fb2bb049a680cc2e731fc891bc.
- Parent reports its built-in OpenDingux UI/PocketSNES and global/per-SNES
  settings VM flow passed. Imported OPK display/input/exit is a separate
  parent-owned VM integration test using the supplied QA OPK.
- No stock-kernel/physical-device OPK execution claim. No card writes.

Touched project paths: only package/lilsmoke-opendingux/,
package/lilsmoke-sqfs/ and tools/opendingux/. Parent's single SITE
trailing-slash adjustment in the new package .mk was preserved.
