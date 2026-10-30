#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Create a small, deterministic source-only ZIP using an explicit allowlist."""
import argparse
import hashlib
from pathlib import Path
import posixpath
import re
import stat
import sys
import zipfile

import lilsmoke_sdk as sdk

FILES = (
    ".gitignore", "LICENSE", "NOTICE", "README.md", "lilsmoke_sdk.py",
    "graphical.py", "runtime_audit.py", "preflight.py", "source_release.py", "docs/lilsmoke-applications-sdk.md",
    "include/lilsmoke/app.h", "include/lilsmoke/sdl.h", "src/lilsmoke_sdl.c",
    "templates/shell/run.sh", "templates/native/run.sh", "templates/native/main.c",
    "templates/native/main.cpp", "templates/sdl2/run.sh", "templates/sdl2/main.c",
    "tests/test_sdk.py", "tests/test_studio.py", "tests/test_source_release.py",
    "tests/release_workflow.py", "tests/integration.py", "tests/sdl_api_test.c",
    "tests/app_header_test.c", "tests/registry_probe.cpp", "tests/recording-app.sh",
    "include/lilsmoke/input.h", "examples/input_state.c", "tests/test_runtime_audit.py",
    "include/lilsmoke/lifecycle.h", "examples/lifecycle.c",
    "tests/lifecycle_test.c", "tests/lifecycle_sdl_test.c",
)
ROOT = "lilsmoke-sdk/"


def check_links(files):
    for name, data in files.items():
        if not name.endswith(".md"):
            continue
        for link in re.findall(r"\[[^\]]*\]\(([^)]+)\)", data.decode("utf-8")):
            path = link.split("#", 1)[0]
            if not path or "://" in path:
                continue
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), path))
            sdk.require(target in files, "Missing/nonportable documentation link: " + name + " -> " + link)


def source_files(source):
    source = source.resolve()
    files = {}
    for name in FILES:
        path = source / name
        sdk.require(path.resolve().is_relative_to(source), "Release source escaped SDK: " + name)
        for parent in path.parents:
            if parent == source:
                break
            sdk.require(not parent.is_symlink(), "Linked source directory: " + name)
        sdk.regular_file(path, 1024 * 1024)
        data = path.read_bytes()
        data.decode("utf-8")
        sdk.require(b"\0" not in data, "Binary/NUL in source file: " + name)
        files[name] = data
    check_links(files)
    return files


def manifest(files):
    return "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n"
                   for name, data in sorted(files.items())).encode("utf-8")


def verify_archive(path):
    sdk.regular_file(path, 4 * 1024 * 1024)
    with zipfile.ZipFile(path) as archive:
        expected = {ROOT + name for name in (*FILES, "SHA256SUMS")}
        sdk.require(set(archive.namelist()) == expected and len(archive.namelist()) == len(expected),
                    "Unexpected or duplicate source release members")
        files = {}
        total = 0
        for item in archive.infolist():
            sdk.require(item.file_size <= 1024 * 1024 and not item.flag_bits & 1,
                        "Oversized/encrypted release member")
            sdk.require(stat.S_IFMT(item.external_attr >> 16) == stat.S_IFREG,
                        "Non-regular source release member")
            total += item.file_size
            sdk.require(total <= 4 * 1024 * 1024, "Oversized source release")
            data = archive.read(item)
            data.decode("utf-8")
            sdk.require(b"\0" not in data, "Binary source release member")
            files[item.filename[len(ROOT):]] = data
        saved = files.pop("SHA256SUMS")
        sdk.require(saved == manifest(files), "Source manifest mismatch")
        check_links(files)
    return dict(source_only=True, files=len(files) + 1, expanded_bytes=total,
                manifest_sha256=hashlib.sha256(saved).hexdigest())


def make_release(output, source=sdk.HERE):
    files = source_files(source)
    files["SHA256SUMS"] = manifest(files)
    sdk.require(output.suffix.lower() == ".zip", "Source release output must end in .zip")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        try:
            with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=False) as archive:
                for name, data in sorted(files.items()):
                    info = zipfile.ZipInfo(ROOT + name, (2020, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    info.external_attr = (stat.S_IFREG | (0o755 if name.endswith(".sh") else 0o644)) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, data)
        except BaseException:
            stream.close()
            output.unlink(missing_ok=True)
            raise
    try:
        result = verify_archive(output)
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    return dict(result, output=str(output.resolve()), sha256=sdk.sha256(output), bytes=output.stat().st_size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(sdk.json_bytes(make_release(args.output)).decode(), end="")
        return 0
    except (sdk.SDKError, OSError, UnicodeError, zipfile.BadZipFile) as error:
        print(sdk.json_bytes(dict(valid=False, error=str(error))).decode(), file=sys.stderr, end="")
        return 1


if __name__ == "__main__":
    sys.exit(main())
