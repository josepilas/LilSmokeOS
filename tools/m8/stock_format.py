#!/usr/bin/env python3
"""Read-only inspection of the supplied HC16xx stock-image format."""
import gzip
import hashlib
from pathlib import Path
import re
import struct
import zlib

XOR_KEY = bytes.fromhex("efbeadde")
SECTOR = 512
BOOT_BYTES = 1024 * 1024


def xor(data, offset=0):
    return bytes(value ^ XOR_KEY[(offset + index) % 4] for index, value in enumerate(data))


def mbr(data, image_bytes):
    if len(data) != SECTOR or data[510:] != b"\x55\xaa":
        raise ValueError("invalid decoded MBR")
    partitions = []
    for slot in range(4):
        entry = data[446 + slot * 16:462 + slot * 16]
        start, count = struct.unpack_from("<II", entry, 8)
        if not any(entry):
            continue
        if entry[0] not in (0, 128) or entry[4] not in (0xb, 0xc) or not start or not count:
            raise ValueError("unexpected stock partition entry")
        if (start + count) * SECTOR > image_bytes:
            raise ValueError("stock partition outside backup")
        if any(start < p["start"] + p["sectors"] and p["start"] < start + count for p in partitions):
            raise ValueError("overlapping stock partitions")
        partitions.append(dict(slot=slot + 1, start=start, sectors=count, type=entry[4]))
    if not partitions:
        raise ValueError("no stock partitions")
    return partitions


def inspect(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise ValueError("only regular backup files are accepted, never a card/device")
    size = path.stat().st_size
    with path.open("rb") as stream:
        prefix = stream.read(BOOT_BYTES)
        if len(prefix) != BOOT_BYTES:
            raise ValueError("truncated stock boot region")
        if prefix[510:512] == b"\x55\xaa":
            encoding, decoded = "plain", prefix[:SECTOR]
        elif xor(prefix[:SECTOR])[510:512] == b"\x55\xaa":
            encoding, decoded = "hc16xx-xor-deadbeef", xor(prefix[:SECTOR])
        else:
            raise ValueError("unknown boot-region encoding")
        partitions = mbr(decoded, size)
        for part in partitions:
            stream.seek(part["start"] * SECTOR)
            boot = stream.read(SECTOR)
            if boot[82:90] != b"FAT32   " or boot[510:] != b"\x55\xaa":
                raise ValueError("stock FAT32 payload is not plain/valid")
            total = struct.unpack_from("<I", boot, 32)[0]
            if not total or total > part["sectors"] or boot[13] not in (1, 2, 4, 8, 16, 32, 64, 128) or \
                    part["sectors"] - total >= boot[13] or struct.unpack_from("<H", boot, 11)[0] != SECTOR:
                raise ValueError("stock filesystem geometry does not match MBR")
            part.update(filesystem="fat32", label=boot[71:82].decode("ascii").rstrip(),
                        cluster_sectors=boot[13], offset_bytes=part["start"] * SECTOR)
    return dict(bytes=size, encoding=encoding, boot_region_sha256=hashlib.sha256(prefix).hexdigest(),
                partitions=partitions, decoded_mbr=decoded, prefix=prefix)


def uimage(path):
    data = Path(path).read_bytes()
    if len(data) < 64:
        raise ValueError("truncated uImage")
    fields = struct.unpack(">7I4B32s", data[:64])
    magic, header_crc, timestamp, size, load, entry, payload_crc, os_id, arch, kind, compression, name = fields
    header = bytearray(data[:64]); header[4:8] = bytes(4)
    if magic != 0x27051956 or zlib.crc32(header) & 0xffffffff != header_crc:
        raise ValueError("invalid uImage header/CRC")
    if len(data) != size + 64 or zlib.crc32(data[64:]) & 0xffffffff != payload_crc or arch != 5:
        raise ValueError("invalid MIPS uImage payload/CRC")
    return dict(name=name.split(b"\0", 1)[0].decode("ascii"), os=os_id, architecture=arch,
                type=kind, compression=compression, load=load, entry=entry,
                bytes=len(data), payload_bytes=size, header_crc=header_crc, payload_crc=payload_crc,
                sha256=hashlib.sha256(data).hexdigest(), timestamp=timestamp)


def kernel_scripts(path):
    image = uimage(path)
    if image["compression"] != 1 or image["type"] != 2:
        raise ValueError("expected gzip Linux kernel")
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    raw = decoder.decompress(Path(path).read_bytes()[64:], 32 * 1024 * 1024 + 1)
    if len(raw) > 32 * 1024 * 1024 or not decoder.eof:
        raise ValueError("kernel exceeds analysis budget")
    versions = re.findall(rb"Linux version[^\0\n]{1,256}", raw)
    for match in re.finditer(b"070701", raw):
        start = pos = match.start()
        scripts, names = {}, []
        try:
            for _ in range(4096):
                if raw[pos:pos + 6] != b"070701":
                    raise ValueError("not an archive boundary")
                fields = [int(raw[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
                size, length = fields[6], fields[11]
                if not 1 <= length <= 4096:
                    raise ValueError("invalid cpio name")
                name = raw[pos + 110:pos + 110 + length - 1].decode("utf-8")
                data_start = start + ((pos - start + 110 + length + 3) // 4) * 4
                if data_start + size > len(raw):
                    raise ValueError("truncated cpio entry")
                content = raw[data_start:data_start + size]
                pos = start + ((data_start - start + size + 3) // 4) * 4
                if name == "TRAILER!!!":
                    if "etc/init.d/S99app" in scripts:
                        return dict(kernel_version=versions[0].decode() if versions else "unknown",
                                    uncompressed_bytes=len(raw), initramfs_offset=start,
                                    initramfs_entries=len(names), scripts=scripts)
                    break
                names.append(name)
                if name == "etc/inittab" or name.startswith("etc/init.d/"):
                    scripts[name] = content.decode("utf-8", "replace")
        except (ValueError, UnicodeError, IndexError):
            continue
    raise ValueError("stock startup archive was not identified")
