#!/usr/bin/env python3
"""Read-only verification of extracted M8 boot dependencies against Candidate06.

Hashes only the eleven BOOT_FILES and usr/lib/libhcfota.so. Never opens an
image, ROM, BIOS archive, updater, save, or unrelated extracted file.
An extracted folder cannot supply the raw MBR/boot gap for a new .img.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat

from build_stock_candidate import BOOT_FILES, STOCK_UI_FILES

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REPORT = ROOT / "build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-06.img.json"
VENDOR_LIBRARY = "usr/lib/libhcfota.so"
HASH_FILES = BOOT_FILES + (VENDOR_LIBRARY,)
MAX_HASH_BYTES = 64 * 1024 * 1024
SHARED_LIBRARY = re.compile(r"(?:lib[^/]+|ld(?:-[^/]+)?)\.so(?:\.[A-Za-z0-9_.-]+)?\Z")
UI_LANGUAGE = re.compile(r"ui_[A-Za-z0-9_-]+\.cpd\Z")
VENDOR_TOOLS = (
    "avpconsole", "dfbplay", "dfbshow", "dfbswitch", "fc-cache", "fc-cat",
    "fc-conflist", "fc-list", "fc-match", "fc-pattern", "fc-query", "fc-scan",
    "fc-validate", "hotplug_helper", "icube", "ldd", "ts_calibrate", "ts_conf",
    "ts_finddev", "ts_harvest", "ts_print", "ts_print_mt", "ts_print_raw",
    "ts_test", "ts_test_mt", "ts_uinput", "ts_verify", "xmlwf",
)
SHARE_ROOTS = ("directfb-1.7.7", "fontconfig", "gettext", "xml")
SHARE_SUFFIXES = {".alias", ".bmp", ".conf", ".cpd", ".dtd", ".jpg", ".jpeg",
                  ".mo", ".png", ".svg", ".ttf", ".xml"}


def _plain(path, directory=False):
    metadata = path.lstat()
    reparse = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    if stat.S_ISLNK(metadata.st_mode) or getattr(metadata, "st_file_attributes", 0) & reparse:
        raise ValueError(f"symlink/reparse point is not a stock source: {path}")
    expected = stat.S_ISDIR if directory else stat.S_ISREG
    if not expected(metadata.st_mode):
        raise ValueError(f"not a regular {'directory' if directory else 'file'}: {path}")
    return metadata


def stock_root(directory):
    directory = Path(directory)
    _plain(directory, directory=True)
    directory = directory.resolve()
    nested = directory / "cubegm"
    if nested.exists() or nested.is_symlink():
        _plain(nested, directory=True)
        directory = nested
    return directory


def selected_file(root, relative):
    parts = PurePosixPath(relative).parts
    if not parts or PurePosixPath(relative).is_absolute() or any(p in (".", "..") for p in parts):
        raise ValueError("unsafe stock-relative path")
    current = root
    for part in parts[:-1]:
        current /= part
        _plain(current, directory=True)
    path = current / parts[-1]
    _plain(path)
    if not path.resolve().is_relative_to(root):
        raise ValueError("stock source escaped its directory")
    return path


def file_hash(path):
    before = _plain(path)
    if before.st_size > MAX_HASH_BYTES:
        raise ValueError(f"stock dependency exceeds the 64 MiB inspection budget: {path}")
    digest = hashlib.sha256()
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(fd, "rb") as stream:
        opened = os.fstat(stream.fileno())
        if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise ValueError(f"stock source replaced while opening: {path}")
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        after = os.fstat(stream.fileno())
    current = _plain(path)
    def signature(value):
        return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns
    if signature(before) != signature(after) or signature(after) != signature(current):
        raise ValueError(f"stock source changed while hashing: {path}")
    return digest.hexdigest(), before.st_size


def reference_hashes(report_path=DEFAULT_REPORT):
    report = json.loads(Path(report_path).read_text(encoding="utf-8"))
    boot = report.get("stock_hashes", {})
    libraries = report.get("required_vendor_libs", {})
    if not isinstance(boot, dict) or set(boot) != set(BOOT_FILES) or \
            not isinstance(libraries, dict) or set(libraries) != {VENDOR_LIBRARY}:
        raise ValueError("candidate report must contain exactly the eleven boot hashes and libhcfota")
    hashes = {**boot, **libraries}
    provenance = report.get("source_img_sha256")
    for value in (*hashes.values(), provenance):
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", value):
            raise ValueError("candidate report contains an invalid SHA-256")
    return {name: value.lower() for name, value in hashes.items()}, provenance.lower()


def check_directory(directory, report_path=DEFAULT_REPORT):
    root = stock_root(directory)
    expected, provenance = reference_hashes(report_path)
    rows, hashed_bytes = {}, 0
    for name in HASH_FILES:
        row = {"expected_sha256": expected[name]}
        try:
            actual, size = file_hash(selected_file(root, name))
            hashed_bytes += size
            row.update(sha256=actual, bytes=size, matches=actual == expected[name])
        except (OSError, ValueError) as error:
            row.update(matches=False, error=str(error))
        rows[name] = row
    return dict(passed=all(row["matches"] for row in rows.values()), read_only=True,
                stock_directory=str(root), candidate_report=str(Path(report_path).resolve()),
                reference_source_img_sha256=provenance, checked_files=len(rows),
                hashed_bytes=hashed_bytes, files=rows, raw_image_read=False,
                rom_data_read=False, raw_boot_gap_required_for_new_image=True)


def _excluded(name):
    return "bios" in name.lower() or name.lower().endswith(".zip") or name.lower() == "hcfota"


def _tree(root, directory, accept):
    current = root
    for part in directory.relative_to(root).parts:
        current /= part
        _plain(current, directory=True)
    _plain(directory, directory=True)
    for base, dirs, names in os.walk(directory, followlinks=False):
        for name in list(dirs):
            if _excluded(name) or name.lower() in {"roms", "saves", "states", "cores"}:
                dirs.remove(name)
            else:
                _plain(Path(base) / name, directory=True)
        for name in sorted(names):
            relative = (Path(base) / name).relative_to(root).as_posix()
            if not _excluded(name) and accept(name):
                yield selected_file(root, relative)


def import_files(root):
    # Enumerate only known content roots, never the console's ROM directories.
    files = {name: selected_file(root, name) for name in BOOT_FILES + STOCK_UI_FILES}
    for entry in root.iterdir():
        if UI_LANGUAGE.fullmatch(entry.name):
            files[entry.name] = selected_file(root, entry.name)
    for relative in ("lib", "usr/lib", "usr/lib32"):
        directory = root / relative
        if not directory.exists() and not directory.is_symlink():
            continue
        _plain(directory, directory=True)
        for entry in directory.iterdir():
            if not _excluded(entry.name) and (SHARED_LIBRARY.fullmatch(entry.name) or entry.name == "os-release"):
                name = entry.relative_to(root).as_posix()
                files[name] = selected_file(root, name)
        if relative.startswith("usr/"):
            for plugins in ("directfb-1.7-7-pure", "ts"):
                plugin_root = directory / plugins
                if plugin_root.exists() or plugin_root.is_symlink():
                    for entry in _tree(root, plugin_root, lambda name: name.endswith(".so")):
                        files[entry.relative_to(root).as_posix()] = entry
    for name in VENDOR_TOOLS:
        relative = "usr/bin/" + name
        path = root / relative
        if path.exists() or path.is_symlink():
            files[relative] = selected_file(root, relative)
    for name in SHARE_ROOTS:
        directory = root / "usr/share" / name
        if directory.exists() or directory.is_symlink():
            for entry in _tree(root, directory, lambda name: Path(name).suffix.lower() in SHARE_SUFFIXES):
                files[entry.relative_to(root).as_posix()] = entry
    if VENDOR_LIBRARY not in files:
        raise ValueError("required libhcfota was not selected for import")
    return files


def copy_directory(directory, destination, report_path=DEFAULT_REPORT, source_img_sha256=None):
    result = check_directory(directory, report_path)
    if not result["passed"]:
        failed = ", ".join(name for name, row in result["files"].items() if not row["matches"])
        raise ValueError("stock directory dependency mismatch: " + failed)
    if source_img_sha256 is not None and source_img_sha256.lower() != result["reference_source_img_sha256"]:
        raise ValueError("raw stock-image provenance differs from the directory reference")
    root, destination = Path(result["stock_directory"]), Path(destination)
    if destination.exists() or destination.is_symlink() or destination.resolve().is_relative_to(root):
        raise ValueError("stock import destination must be new and outside the read-only source")
    files = import_files(root)
    destination.mkdir()
    for name, source in sorted(files.items()):
        source = selected_file(root, name)
        before = source.stat()
        output = destination / name
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, output)
        after = source.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise ValueError("stock source changed during import: " + name)
    for name, row in result["files"].items():
        if file_hash(destination / name)[0] != row["expected_sha256"]:
            raise ValueError("copied stock dependency mismatch: " + name)
    result["imported_files"] = sorted(files)
    result["imported_count"] = len(files)
    return {name: result["files"][name]["sha256"] for name in BOOT_FILES}, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock-directory", type=Path, required=True,
                        help="extracted card root or its cubegm directory; read-only")
    parser.add_argument("--candidate-report", type=Path, default=DEFAULT_REPORT,
                        help="Candidate06 JSON containing stock_hashes and required_vendor_libs")
    args = parser.parse_args()
    try:
        result = check_directory(args.stock_directory, args.candidate_report)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
