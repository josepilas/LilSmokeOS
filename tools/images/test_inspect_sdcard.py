#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("inspector", Path(__file__).with_name("inspect_sdcard.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def checksum(region):
    value = 0
    for index, byte in enumerate(region[:11 * 512]):
        if index not in (106, 107, 112):
            value = (((value & 1) << 31) + (value >> 1) + byte) & 0xffffffff
    region[11 * 512:12 * 512] = struct.pack("<I", value) * 128


def fixture():
    data = bytearray(6144 * 512)
    data[510:512] = b"\x55\xaa"
    for i, (start, kind) in enumerate(((2048, 12), (4096, 7))):
        offset = 446 + i * 16
        data[offset] = 0x80 if i == 0 else 0
        data[offset + 4] = kind
        struct.pack_into("<II", data, offset + 8, start, 2048)
    fat = 2048 * 512
    data[fat + 82:fat + 90] = b"FAT32   "
    data[fat + 71:fat + 82] = b"LILOS      "
    struct.pack_into("<H", data, fat + 11, 512)
    data[fat + 13] = 8
    struct.pack_into("<I", data, fat + 32, 2048)
    data[fat + 510:fat + 512] = b"\x55\xaa"
    exfat = 4096 * 512
    region = bytearray(12 * 512)
    region[3:11] = b"EXFAT   "
    struct.pack_into("<QQ", region, 64, 4096, 2048)
    struct.pack_into("<I", region, 88, 128)
    struct.pack_into("<I", region, 96, 2)
    region[108], region[109] = 9, 3
    region[510:512] = b"\x55\xaa"
    checksum(region)
    data[exfat:exfat + len(region)] = region
    root = exfat + 128 * 512
    data[root], data[root + 1] = 0x83, 7
    data[root + 2:root + 16] = "LILROMS".encode("utf-16-le")
    return data


class ImageTests(unittest.TestCase):
    def check(self, data, valid=True, lilsmoke=True):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "card.img"
            path.write_bytes(data)
            before = path.read_bytes()
            if valid:
                result = module.inspect(path, lilsmoke)
                self.assertFalse(result["physical_boot_verified"])
            else:
                with self.assertRaises(ValueError):
                    module.inspect(path, lilsmoke)
                result = None
            self.assertEqual(path.read_bytes(), before)
            return result

    def test_standard_raw_layout(self):
        result = self.check(fixture())
        self.assertEqual([p["archive_view"] for p in result["partitions"]], ["0.fat", "1.exfat"])
        self.assertEqual([p["label"] for p in result["partitions"]], ["LILOS", "LILROMS"])

    def test_cluster_rounding(self):
        data = fixture()
        struct.pack_into("<I", data, 2048 * 512 + 32, 2044)
        self.check(data)

    def test_manual_fat32_with_original_type7(self):
        data = fixture()
        data[4096 * 512:4097 * 512] = data[2048 * 512:2049 * 512]
        data[4096 * 512 + 71:4096 * 512 + 82] = b"LILROMS    "
        result = self.check(data)
        self.assertEqual(result["partitions"][1]["type"], "07")
        self.assertEqual(result["partitions"][1]["archive_view"], "1.fat")

    def test_missing_signature(self):
        data = fixture(); data[510] = 0
        self.check(data, False)

    def test_overlap(self):
        data = fixture(); struct.pack_into("<I", data, 470, 2050)
        self.check(data, False)

    def test_outside_image(self):
        data = fixture(); struct.pack_into("<I", data, 474, 999999)
        self.check(data, False)

    def test_bad_exfat_checksum(self):
        data = fixture(); data[4096 * 512 + 77] ^= 1
        self.check(data, False)

    def test_wrong_label(self):
        data = fixture(); data[2048 * 512 + 71] = 65
        self.check(data, False)

    def test_truncated(self):
        self.check(fixture()[:-512], False)

    def test_not_sector_aligned(self):
        self.check(fixture()[:-1], False)

    def test_invalid_boot_flag(self):
        data = fixture(); data[446] = 3
        self.check(data, False)

    def test_wrong_filesystem_size(self):
        data = fixture(); struct.pack_into("<I", data, 2048 * 512 + 32, 512)
        self.check(data, False)

    def test_boot_code_is_not_boot_verification(self):
        data = fixture(); data[0] = 0x90
        self.assertTrue(self.check(data)["boot_code_present"])

    def test_third_partition_rejected(self):
        data = fixture(); data.extend(bytes(2048 * 512))
        data[482] = 0x83
        struct.pack_into("<II", data, 486, 6144, 2048)
        self.check(data, False)
        self.assertEqual(len(self.check(data, lilsmoke=False)["partitions"]), 3)

    def test_extended_partition_rejected(self):
        data = fixture(); data[466] = 15
        self.check(data, False)

    def test_btrfs_identification(self):
        data = fixture(); data.extend(bytes(2048 * 512))
        data[482] = 0x83
        struct.pack_into("<II", data, 486, 6144, 2048)
        superblock = 6144 * 512 + 65536
        data[superblock + 64:superblock + 72] = b"_BHRfS_M"
        struct.pack_into("<Q", data, superblock + 112, 2048 * 512)
        data[superblock + 299:superblock + 303] = b"ROOT"
        result = self.check(data, lilsmoke=False)
        self.assertEqual(result["partitions"][2]["filesystem"], "btrfs")
        self.assertEqual(result["partitions"][2]["label"], "ROOT")
        self.assertEqual(result["partitions"][2]["archive_view"], "2.img")


if __name__ == "__main__":
    unittest.main()
