# Parent Integration Contract

Owned paths: package/lilsmoke-opendingux, package/lilsmoke-sqfs,
tools/opendingux. No parent-owned UI, registry, settings, external Config,
build orchestration, existing packages, or card files are edited here.

- Source both new package Config.in files from external Config.in.
- Enable BR2_PACKAGE_LILSMOKE_OPENDINGUX=y (selects LILSMOKE_SQFS, zlib, xz, libsodium).
- Build lilsmoke-opendingux; dependency lilsmoke-sqfs (libsquashfs 1.2.0).
- Helper: /usr/lib/lilsmoke/lilsmoke-opendingux.
- scan: five TSV fields: id, name, systemsCSV, reason, needsRom (0/1).
  Empty reason means supported; every record ends in newline. IDs contain
  only safe ASCII, currently od- plus 64 lowercase SHA256 hex digits.
- run ID [ROM]: no shell command evaluation. Parent resolves ROM archives
  first. Runtime diagnostics go to stderr, bounded to 191 bytes.
- Catalog roots: /storage/opendingux/apps and
  /usr/share/lilsmoke/opendingux/apps; cache /storage/opendingux/cache;
  per-ID HOME /persist/opendingux/home/ID. Cache must be writable AND
  executable (FAT mounted noexec cannot launch native applications).
- Shipped pocketsnes.desktop may be deduplicated against od:pocketsnes.
  Its systems field is snes and needsRom is 1.
  Fixed runtime ID: od-b4f617046bce9188181455925915bbe3b02e2ba87f238e6bf8570eef67fa3cf1.
- Parent must suspend/release UI presentation/audio resources before run,
  then restore its normal application supervisor on return.

This is a native ABI subset, not universal OpenDingux binary compatibility.
No fake libc loader symlink, loop device, FUSE, GPU, or legacy uClibc loader.
