#!/usr/bin/env python3
"""Losslessly archive a regular image and verify it before any caller cleanup."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    if not source.is_file() or args.source.is_symlink() or source.suffix != ".img":
        parser.error("expected a regular .img file, never a device")
    output = source.with_suffix(".img.xz")
    before = source.stat()
    digest = hashlib.sha256()
    with source.open("rb") as stream, output.open("xb") as target:
        with lzma.LZMAFile(target, "w", preset=3) as archive:
            for data in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(data)
                archive.write(data)
    verified, size = hashlib.sha256(), 0
    with lzma.open(output, "rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            verified.update(data)
            size += len(data)
    after = source.stat()
    if digest.digest() != verified.digest() or size != before.st_size or \
            (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise RuntimeError("archive verification failed; source was retained")
    print(json.dumps(dict(source=str(source), archive=str(output), bytes=size,
                          sha256=digest.hexdigest(), verified=True, source_removed=False), indent=2))


if __name__ == "__main__":
    main()
