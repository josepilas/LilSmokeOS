#!/usr/bin/env python3
"""Synthetic parser guards and read-only regression tests for the supplied backup."""
import json
import struct
import unittest
from pathlib import Path

from inventory_backup import records
from prepare_image_plan import build_plan
from probe_backup import describe, fdt, protection_manifest

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "build/r36sx-2.7-20261022"


def tiny_fdt():
    names = b"test\0"
    structure = struct.pack(">I", 1) + bytes(4)
    structure += struct.pack(">IIII", 3, 4, 0, 640)
    structure += struct.pack(">II", 2, 9)
    return struct.pack(">10I", 0xD00DFEED, 56 + len(structure) + len(names),
                       56, 56 + len(structure), 40, 17, 16, 0, len(names), len(structure)) + bytes(16) + structure + names


class ParserTests(unittest.TestCase):
    def test_numeric_zero_is_not_an_empty_string(self):
        self.assertEqual(describe(bytes(4))["cells"], [0])

    def test_fdt_property(self):
        self.assertEqual(fdt(tiny_fdt())["/"]["test"], struct.pack(">I", 640))

    def test_truncated_fdt_rejected(self):
        with self.assertRaises(ValueError):
            fdt(tiny_fdt()[:-1])

    def test_unbalanced_fdt_rejected(self):
        blob = bytearray(tiny_fdt())
        struct.pack_into(">I", blob, 80, 9)
        with self.assertRaises(ValueError):
            fdt(blob)

    def test_oversized_property_rejected(self):
        blob = bytearray(tiny_fdt())
        struct.pack_into(">I", blob, 68, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            fdt(blob)

    def test_archive_listing(self):
        self.assertEqual(list(records("Header\n----------\nPath = root/cubegm/dtb.bin\nSize = 12\nAttributes = A\nCRC = ABCD\n"))[0]["size"], 12)

    def test_archive_traversal_rejected(self):
        for name in ("../outside", "root/../../outside", "C:/outside", "/outside"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                list(records("----------\nPath = " + name + "\nSize = 1\n"))


@unittest.skipUnless((EVIDENCE / "probe.json").is_file(), "supplied backup analysis not generated")
class BackupTests(unittest.TestCase):
    def setUp(self):
        self.inventory = json.loads((EVIDENCE / "inventory.json").read_text())
        self.probe = json.loads((EVIDENCE / "probe.json").read_text())
        self.manifest = (EVIDENCE / "selected/usbimager-20260530T0359/cubegm/AcXtrnal").read_bytes()

    def test_all_eighteen_protected_files(self):
        result = protection_manifest(self.manifest, self.inventory)
        self.assertEqual(len(result["entries"]), 18)
        self.assertTrue(result["all_archive_crc_matches"])

    def test_changed_protected_crc_rejected(self):
        for row in self.inventory["members"]:
            if row["path"].endswith("/cubegm/driver.so"):
                row["crc32"] = "00000000"
        with self.assertRaises(ValueError):
            protection_manifest(self.manifest, self.inventory)

    def test_exact_panel_two_partitions(self):
        plan = build_plan(self.inventory, self.probe)
        self.assertEqual(plan["initial_settings"]["DISPLAY_WIDTH"], 640)
        self.assertEqual(plan["initial_settings"]["DISPLAY_HEIGHT"], 480)
        self.assertEqual([row["label"] for row in plan["partitions"]], ["LILOS", "LILROMS"])
        self.assertEqual(plan["partitions"][1]["start_sector"], 3147776)
        self.assertFalse(plan["image_generated"])

    def test_other_stock_lineage_rejected(self):
        self.inventory["sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            build_plan(self.inventory, self.probe)

    def test_widescreen_m8_geometry_rejected(self):
        self.probe["facts"]["boot_framebuffer"]["xres"]["cells"] = [1280]
        with self.assertRaises(ValueError):
            build_plan(self.inventory, self.probe)

    def test_missing_stock_driver_rejected(self):
        self.inventory["members"] = [row for row in self.inventory["members"]
                                      if not row["path"].endswith("/cubegm/driver.so")]
        with self.assertRaises(ValueError):
            build_plan(self.inventory, self.probe)

    def test_only_memory_addresses_differ_between_trees(self):
        differences = self.probe["dtb_variant_differences"]
        self.assertEqual(len(differences), 7)
        for row in differences:
            left, right = row["dtb"]["cells"], row["bubbles"]["cells"]
            index = 1 if row["node"] == "/memory" else 0
            self.assertEqual(left[index] - right[index], 0x08000000)
            self.assertEqual(left[1-index], right[1-index])


if __name__ == "__main__":
    unittest.main()
