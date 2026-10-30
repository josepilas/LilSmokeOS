#!/usr/bin/env python3
"""Repair only the known 12-sector formatter rounding in an unverified FILE image.

Preserve every file, FAT chain and MBR. Save exact metadata before modification.
Never opens a block device, published image, symlink or a differently shaped image.
"""
import argparse
import json
import os
import struct
from pathlib import Path
from build_candidate import ARCHIVE_SHA256

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--stage", type=Path, required=True)
p.add_argument("--image", type=Path, required=True)
a = p.parse_args()
report = json.loads((a.stage / "stage.json").read_text())
if report["archive_sha256"] != ARCHIVE_SHA256 or report["physical_boot_verified"]:
    raise ValueError("Wrong owned stage lineage")
if a.image.is_symlink() or not a.image.is_file() or a.image.suffix != ".img":
    raise ValueError("Only the owned unverified regular image is accepted")
if a.image.with_suffix(".img.json").exists() or a.image.with_suffix(".img.sha256").exists():
    raise ValueError("Do not modify a qualified/published image")
parts = report["partitions"]
if a.image.stat().st_size != (parts[1]["start_sector"] + parts[1]["sectors"]) * 512:
    raise ValueError("Unexpected file length")
with a.image.open("r+b") as file:
    mbr = file.read(512)
    if mbr[510:] != b"\x55\xaa":
        raise ValueError("Wrong MBR")
    for index, part in enumerate(parts):
        if struct.unpack_from("<II", mbr, 446 + index * 16 + 8) != (part["start_sector"], part["sectors"]):
            raise ValueError("Unexpected partition map")
    base = parts[0]["start_sector"] * 512
    file.seek(base)
    boot = bytearray(file.read(512))
    old = struct.unpack_from("<I", boot, 32)[0]
    total = parts[0]["sectors"]
    reserved, fat = struct.unpack_from("<H", boot, 14)[0], struct.unpack_from("<I", boot, 36)[0]
    info, backup = struct.unpack_from("<HH", boot, 48)
    if total != 3145728 or old != total - 12 or boot[13] != 8 or boot[16] != 2 or reserved != 64 or fat != 3072:
        raise ValueError("Not the exact known formatter rounding; refusing general repair")
    if boot[71:82] != b"LILOS      " or boot[82:90] != b"FAT32   " or (info, backup) != (1, 6):
        raise ValueError("Unexpected LILOS BPB")
    file.seek(base + backup * 512)
    if file.read(512) != boot:
        raise ValueError("Original boot backups differ")
    file.seek(base + info * 512)
    fsinfo = bytearray(file.read(512))
    file.seek(base + (backup + info) * 512)
    if file.read(512) != fsinfo:
        raise ValueError("Original FSInfo backups differ")
    old_clusters = (old - reserved - 2 * fat) // 8
    new_clusters = (total - reserved - 2 * fat) // 8
    if new_clusters - old_clusters != 2 or fat * 128 < new_clusters + 2:
        raise ValueError("Unexpected FAT capacity")
    for mirror in range(2):
        file.seek(base + (reserved + mirror * fat) * 512 + (old_clusters + 2) * 4)
        if file.read(8) != b"\0" * 8:
            raise ValueError("Newly exposed FAT entries are not free")
    before = {"image": str(a.image.resolve()), "operation": "known 12-sector BPB rounding only; no files/FAT/MBR changed",
              "old_clusters": old_clusters, "new_clusters": new_clusters,
              "sectors": {str(base + n * 512): data.hex() for n, data in
                          ((0, boot), (backup, boot), (info, fsinfo), (backup + info, fsinfo))}}
    log = a.image.with_suffix(".img.geometry-before.json")
    with log.open("x") as save:
        json.dump(before, save, indent=2)
        save.write("\n")
    struct.pack_into("<I", boot, 32, total)
    free = struct.unpack_from("<I", fsinfo, 488)[0]
    if free != 0xFFFFFFFF:
        if free > old_clusters:
            raise ValueError("Impossible original free count")
        struct.pack_into("<I", fsinfo, 488, free + 2)
    for sector, data in ((backup + info, fsinfo), (info, fsinfo), (backup, boot), (0, boot)):
        file.seek(base + sector * 512)
        file.write(data)
    file.flush();os.fsync(file.fileno())
    for sector, data in ((0, boot), (backup, boot), (info, fsinfo), (backup + info, fsinfo)):
        file.seek(base + sector * 512)
        if file.read(512) != data:
            raise ValueError("Metadata readback failed")
print("PASS corrected only LILOS BPB length/free count and matching backups; all file data, FAT chains and MBR preserved")
