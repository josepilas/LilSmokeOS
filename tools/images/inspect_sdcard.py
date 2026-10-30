#!/usr/bin/env python3
"""Read a raw MBR image without mounting it or writing to any device."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

SECTOR = 512


def read_at(stream, offset, length):
    stream.seek(offset)
    data = stream.read(length)
    if len(data) != length:
        raise ValueError("truncated image")
    return data


def inspect(path, lilsmoke=False):
    path = Path(path)
    if not path.is_file():
        raise ValueError("only regular image files are accepted")
    size = path.stat().st_size
    if size % SECTOR:
        raise ValueError("image size is not sector aligned")
    parts = []
    with path.open("rb") as stream:
        mbr = read_at(stream, 0, SECTOR)
        if mbr[510:] != b"\x55\xaa":
            raise ValueError("missing MBR signature (not a raw disk image)")
        for slot in range(4):
            entry = mbr[446 + slot * 16:462 + slot * 16]
            boot, kind = entry[0], entry[4]
            start, count = struct.unpack_from("<II", entry, 8)
            if not kind and not start and not count:
                continue
            if boot not in (0, 0x80) or not kind or not count or not start:
                raise ValueError("invalid primary partition entry")
            if kind in (5, 15, 0x85, 0xee):
                raise ValueError("extended/GPT partitions are not supported by this verifier")
            if (start + count) * SECTOR > size:
                raise ValueError("partition extends beyond image")
            if any(start < p["start_sector"] + p["sectors"] and
                   p["start_sector"] < start + count for p in parts):
                raise ValueError("overlapping partitions")
            offset = start * SECTOR
            boot_sector = read_at(stream, offset, SECTOR)
            fs, label, fs_sectors = "unknown", "", 0
            if boot_sector[3:11] == b"EXFAT   ":
                fs = "exfat"
                if kind != 7 or boot_sector[108] != 9:
                    raise ValueError("unexpected exFAT type or sector size")
                fs_sectors = struct.unpack_from("<Q", boot_sector, 72)[0]
                if struct.unpack_from("<Q", boot_sector, 64)[0] != start:
                    raise ValueError("exFAT partition offset does not match MBR")
                region = read_at(stream, offset, 12 * SECTOR)
                checksum = 0
                for index, byte in enumerate(region[:11 * SECTOR]):
                    if index not in (106, 107, 112):
                        checksum = (((checksum & 1) << 31) + (checksum >> 1) + byte) & 0xffffffff
                if any(word != checksum for word in struct.unpack("<128I", region[11 * SECTOR:])):
                    raise ValueError("invalid exFAT boot checksum")
                # The volume label is a directory entry, not a BPB field.
                cluster = struct.unpack_from("<I", boot_sector, 96)[0]
                heap = struct.unpack_from("<I", boot_sector, 88)[0]
                cluster_sectors = 1 << boot_sector[109]
                if boot_sector[109] > 16 or cluster < 2:
                    raise ValueError("invalid exFAT root geometry")
                root_sector = heap + (cluster - 2) * cluster_sectors
                if root_sector + cluster_sectors > fs_sectors:
                    raise ValueError("exFAT root outside filesystem")
                root = read_at(stream, offset + root_sector * SECTOR,
                               min(cluster_sectors * SECTOR, 65536))
                for index in range(0, len(root), 32):
                    record = root[index:index + 32]
                    if record[0] == 0:
                        break
                    if record[0] == 0x83 and record[1] <= 11:
                        label = record[2:2 + record[1] * 2].decode("utf-16-le")
                        break
            elif boot_sector[82:90] == b"FAT32   ":
                fs = "fat32"
                if kind not in (0x7, 0xb, 0xc) or struct.unpack_from("<H", boot_sector, 11)[0] != SECTOR:
                    raise ValueError("unexpected FAT32 partition type or sector size")
                if boot_sector[13] not in (1, 2, 4, 8, 16, 32, 64, 128):
                    raise ValueError("invalid FAT32 allocation unit")
                fs_sectors = struct.unpack_from("<I", boot_sector, 32)[0]
                label = boot_sector[71:82].decode("ascii").rstrip()
            elif count >= 3:
                superblock = read_at(stream, offset + 1024, 1024)
                if superblock[56:58] == b"\x53\xef":
                    fs = "ext"
                    label = superblock[120:136].split(b"\0", 1)[0].decode("utf-8", "replace")
                elif count >= 136:
                    superblock = read_at(stream, offset + 65536, 4096)
                    if superblock[64:72] == b"_BHRfS_M":
                        fs = "btrfs"
                        label = superblock[299:555].split(b"\0", 1)[0].decode("utf-8", "replace")
                        fs_sectors = struct.unpack_from("<Q", superblock, 112)[0] // SECTOR
                        if not fs_sectors or fs_sectors > count:
                            raise ValueError("Btrfs filesystem extends outside partition")
            if fs in ("exfat", "fat32"):
                if boot_sector[510:] != b"\x55\xaa" or not fs_sectors or fs_sectors > count:
                    raise ValueError("invalid filesystem signature/size")
                allocation_unit = (1 << boot_sector[109]) if fs == "exfat" else boot_sector[13]
                if not allocation_unit or count - fs_sectors >= allocation_unit:
                    raise ValueError("filesystem does not fill its partition (apart from cluster rounding)")
            extension = {"fat32": "fat", "exfat": "exfat"}.get(fs, "img")
            parts.append(dict(slot=slot + 1, start_sector=start, sectors=count,
                              offset_bytes=offset, size_bytes=count * SECTOR,
                              type=f"{kind:02x}", bootable=bool(boot), filesystem=fs,
                              label=label, filesystem_sectors=fs_sectors,
                              archive_view=f"{len(parts)}.{extension}"))
    if lilsmoke:
        if len(parts) != 2 or [p["label"] for p in parts] != ["LILOS", "LILROMS"]:
            raise ValueError("LilSmokeOS requires exactly LILOS and LILROMS partitions")
        if parts[0]["filesystem"] != "fat32" or parts[1]["filesystem"] not in ("fat32", "exfat"):
            raise ValueError("expected FAT32 LILOS and exFAT/FAT32 LILROMS")
        if any(p["start_sector"] % 2048 for p in parts):
            raise ValueError("LilSmokeOS partitions must be MiB aligned")
    return dict(image=str(path.resolve()), bytes=size, partition_table="mbr",
                boot_code_present=any(mbr[:440]), partitions=parts,
                trailing_free_sectors=size // SECTOR - max(p["start_sector"] + p["sectors"] for p in parts),
                physical_boot_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--lilsmoke", action="store_true")
    parser.add_argument("--sha256", action="store_true")
    args = parser.parse_args()
    try:
        result = inspect(args.image, args.lilsmoke)
        if args.sha256:
            digest = hashlib.sha256()
            with args.image.open("rb") as stream:
                for data in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(data)
            result["sha256"] = digest.hexdigest()
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, UnicodeError) as error:
        print(f"image verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
