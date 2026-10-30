#!/usr/bin/env python3
"""Pure builder boundary tests. No image or block device is created."""
import struct
import tempfile
import unittest
from pathlib import Path
from build_candidate import patch_autorun, plain_tree, validate_runtime, verify_component, verify_stock, handheld_retroarch, verify_fat32


class CandidateTests(unittest.TestCase):
    def test_autorun_only_absolute_and_driver_empty(self):
        result = patch_autorun('<?xml version="1.0"?><setting sound="yes"/><autorun file="old" driver="bad"/>')
        self.assertIn('file="/mnt/sdcard/MD/lilsmoke.md"', result)
        self.assertIn('driver=""', result)
        self.assertIn('sound="yes"', result)

    def test_reject_ambiguous_autorun(self):
        for text in ("<setting/>", "<autorun/><autorun/>"):
            with self.assertRaises(ValueError):
                patch_autorun(text)

    def test_handheld_retroarch_exact_patch(self):
        original = ('        menu_ratio=1\n        menu_lock=3\n'
                    '        [ "$MODE" != menu ] || video_aspect=1.777778\n')
        patched = handheld_retroarch(original)
        self.assertIn("menu_ratio=0", patched)
        self.assertIn("video_aspect=1.333333", patched)
        for value in ("unrelated", original + original, patched):
            with self.assertRaises(ValueError):
                handheld_retroarch(value)

    def test_plain_tree_regular_files(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "hello").write_bytes(b"hello")
            result = plain_tree(root)
            self.assertEqual(result["hello"]["bytes"], 5)
            self.assertEqual(len(result["hello"]["sha256"]), 64)

    def test_protected_checksum(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "stock").write_bytes(b"hello")
            plan = {"stock_preserve_byte_exact": {"stock": {"size": 5, "crc32": "3610A686"}}}
            verify_stock(root, plan)
            (root / "stock").write_bytes(b"Hello")
            with self.assertRaises(ValueError):
                verify_stock(root, plan)

    def test_manifest_guards(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "files").mkdir()
            (root / "files/000001.bin").write_bytes(b"sample")
            base = ["F\t0755\tusr/bin/lilsmoke-ui\t000001.bin", "F\t0755\tbin/busybox\t000001.bin",
                    "F\t0755\tusr/lib/lilsmoke/m8-safe-power\t000001.bin"]
            base += [f"D\t0755\tdir{i}\t" for i in range(7)]
            manifest = root / "runtime.tsv"
            manifest.write_text("\n".join(base))
            self.assertEqual(validate_runtime(root)[1], {"000001.bin"})
            for extra in ("D\t0755\t../escape\t", "D\t0755\tbin/busybox\t", "F\t0755\tmissing\t000002.bin"):
                manifest.write_text("\n".join(base + [extra]))
                with self.assertRaises(ValueError):
                    validate_runtime(root)

    def test_component_abi_and_dependencies(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "plugin"
            elf = bytearray(100)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<H", elf, 18, 8)
            struct.pack_into("<I", elf, 28, 52)
            struct.pack_into("<HH", elf, 42, 32, 1)
            struct.pack_into("<8I", elf, 52, 2, 84, 0, 0, 16, 16, 0, 4)
            file.write_bytes(elf)
            verify_component(file, freestanding=True)
            struct.pack_into("<II", elf, 84, 1, 9)
            file.write_bytes(elf)
            with self.assertRaises(ValueError):
                verify_component(file, freestanding=True)
            struct.pack_into("<I", elf, 52, 3)
            file.write_bytes(elf)
            with self.assertRaises(ValueError):
                verify_component(file, standalone=True)
            elf[5] = 2
            file.write_bytes(elf)
            with self.assertRaises(ValueError):
                verify_component(file)

    def test_fat32_classification_and_backups(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "bpb-fixture"
            def fixture(clusters):
                start, spc, reserved, fats, fat = 2048, 16, 64, 2, 1024
                sectors = clusters * spc + reserved + fats * fat
                boot = bytearray(512)
                struct.pack_into("<H", boot, 11, 512)
                boot[13] = spc
                struct.pack_into("<H", boot, 14, reserved)
                boot[16] = fats
                struct.pack_into("<I", boot, 28, start)
                struct.pack_into("<I", boot, 32, sectors)
                struct.pack_into("<I", boot, 36, fat)
                struct.pack_into("<HH", boot, 48, 1, 6)
                boot[71:82] = b"LILROMS    "
                boot[82:90] = b"FAT32   "
                boot[510:] = b"\x55\xaa"
                info = bytearray(512)
                info[:4], info[484:488], info[508:] = b"RRaA", b"rrAa", b"\0\0\x55\xaa"
                with file.open("wb") as target:
                    for offset, value in ((0, boot), (1, info), (6, boot), (7, info)):
                        target.seek((start + offset) * 512)
                        target.write(value)
                return start, sectors
            start, sectors = fixture(65524)
            with self.assertRaises(ValueError):
                verify_fat32(file, start, sectors)
            start, sectors = fixture(65525)
            self.assertEqual(verify_fat32(file, start, sectors)["windows_classification"], "FAT32")
            with file.open("r+b") as target:
                target.seek((start + 7) * 512 + 10)
                target.write(b"x")
            with self.assertRaises(ValueError):
                verify_fat32(file, start, sectors)


if __name__ == "__main__":
    unittest.main(verbosity=2)
