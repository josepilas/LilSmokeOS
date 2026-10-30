#!/usr/bin/env python3
"""Read-only boot diagnosis for a mounted LILOS folder (never a physical disk)."""
# SPDX-License-Identifier: MPL-2.0
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

SIZE, COUNT = 512, 32


def decode_journal(data):
    if len(data) != SIZE * COUNT:
        raise ValueError("Boot journal must be exactly 16 KiB")
    records, invalid = [], []
    for slot in range(COUNT):
        record = data[slot * SIZE:(slot + 1) * SIZE]
        if record == bytes(SIZE):
            continue
        seq, ms, pid, length = struct.unpack_from("<4I", record, 8)
        checksum = struct.unpack_from("<I", record, 508)[0]
        if (record[:8] != b"LSBJ0001" or not seq or length > 479 or
                zlib.crc32(record[:508]) != checksum or
                any(c < 32 or c > 126 for c in record[24:24 + length])):
            invalid.append(slot)
            continue
        records.append({"sequence": seq, "uptime_ms": ms, "pid": pid,
                        "event": record[24:24 + length].decode("ascii"), "slot": slot})
    records.sort(key=lambda row: row["sequence"])
    return {"records": records, "invalid_slots": invalid,
            "last_checkpoint": records[-1]["event"] if records else None}


def regular(root, relative):
    path = root
    for part in Path(relative).parts:
        if part in ("..", "."):
            raise ValueError("Invalid diagnostic manifest path")
        path = path / part
        if path.is_symlink():
            raise ValueError("Do not follow links on a returned card")
    if not path.is_file():
        raise ValueError("Missing diagnostic file: " + relative)
    return path


def inspect_folder(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Pass the mounted LILOS folder, not a device/image")
    manifest = regular(root, "lilsmoke/boot-manifest.json")
    if manifest.stat().st_size > 128 * 1024:
        raise ValueError("Diagnostic manifest too large")
    metadata = json.loads(manifest.read_text(encoding="ascii"))
    protected = metadata["protected"]
    if len(protected) > 64:
        raise ValueError("Too many protected files")
    mismatches = []
    for relative, expected in protected.items():
        if (not (relative.startswith("cubegm/") or relative == "rootfs/usr/bin/cubevol")
                or "\\" in relative or ":" in relative):
            raise ValueError("Unsafe protected path")
        path = regular(root, relative)
        if path.stat().st_size > 64 * 1024 * 1024:
            raise ValueError("Unexpected boot file size")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != expected:
            mismatches.append(relative)
    journal = regular(root, "lilsmoke/boot-journal.bin")
    if journal.stat().st_size != SIZE * COUNT:
        raise ValueError("Invalid journal length")
    report = decode_journal(journal.read_bytes())
    report.update(profile=metadata["profile"], protected_mismatches=mismatches,
                  physical_boot_verified=False, original_clock_not_required=True)
    if not report["records"]:
        report["diagnosis"] = (
            "No authored checkpoint observed. This does not prove that Linux did not boot. "
            "The firmware may not reach either hook, the journal may be unwritable, or "
            "failure may occur earlier. Verify the device/backup match and collect UART "
            "boot output if both SD entry paths remain untouched.")
    else:
        report["diagnosis"] = "Last persisted checkpoint: " + report["last_checkpoint"]
        if report["invalid_slots"]:
            report["diagnosis"] += "; torn/corrupt records ignored, older valid checkpoints retained"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lilos_folder", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_folder(args.lilos_folder), indent=2))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, "Read-only diagnosis could not finish: " + str(error) + "\n")


if __name__ == "__main__":
    main()
