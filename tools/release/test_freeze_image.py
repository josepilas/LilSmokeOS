#!/usr/bin/env python3
"""Focused fail-closed tests for the regular-image release freezer."""
import copy
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zipfile

import freeze_image as freezer


class FreezeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        freezer.DESTINATION.mkdir(parents=True, exist_ok=True)

    def test_shifted_geometry_preserves_boot_code_and_gap(self):
        original = bytes(range(256)) * 4096
        changed = freezer.new_prefix({"prefix": original}, 1024, 768)
        self.assertEqual(changed[:446], original[:446])
        self.assertEqual(changed[510:], original[510:])
        self.assertEqual(struct.unpack_from("<II", changed, 454), (2048, 2097152))
        self.assertEqual(struct.unpack_from("<II", changed, 470), (2099200, 1572864))
        self.assertLess((2048 + 2097152 + 1572864) * 512, 2 * 1024**3)

    def test_manifest_rejects_traversal_duplicates_and_foreign_objects(self):
        for text in ("F\t0644\t../bad\t000000.bin\n", "F\t0644\tbin/a\t../a\n",
                     "D\t0755\tbin\t\nD\t0755\tBIN\t\n", "F\t0644\t/x\t000001.bin\n"):
            with self.assertRaises(ValueError):
                freezer.parse_manifest(text)

    def test_exact_prototype_removal_and_shared_object_refusal(self):
        with tempfile.TemporaryDirectory(dir=freezer.DESTINATION) as temporary:
            payload = Path(temporary)
            (payload / "files").mkdir()
            rows = [["D" if name.endswith("/sm64") else "F", "0755", name,
                     "" if name.endswith("/sm64") else f"{i:06d}.bin"]
                    for i, name in enumerate(sorted(freezer.PROTOTYPE))]
            for row in rows:
                if row[3]:
                    (payload / "files" / row[3]).write_bytes(b"prototype")
            shared = copy.deepcopy(rows)
            obj = next(r[3] for r in rows if r[3])
            shared.append(["F", "0644", "usr/bin/retained", obj])
            with self.assertRaises(ValueError):
                freezer.remove_prototype(shared, payload)
            survivors, removed = freezer.remove_prototype(rows, payload)
            self.assertFalse(survivors)
            self.assertEqual(len(removed), 5)
            self.assertFalse(list((payload / "files").iterdir()))

    def test_private_assets_and_keys_rejected_even_when_renamed(self):
        for name, header in (("renamed.bin", freezer.ROM_MAGICS[0]), ("roms/a.bin", b""),
                             ("bios/psx.bin", b""), ("etc/ssh_host_rsa_key", b""),
                             ("usr/bin/sm64-original", b"\x7fELF"), ("x/.portsmoke/cache", b"")):
            with self.assertRaises(ValueError):
                freezer.reject_private(name, header)
        freezer.reject_private("usr/sbin/dropbear", b"\x7fELF")
        freezer.reject_private("usr/share/licenses/LICENSE.txt", b"notice")

    def test_zip_member_audit_rejects_game_and_nested_archive(self):
        with tempfile.TemporaryDirectory(dir=freezer.DESTINATION) as temporary:
            path = Path(temporary) / "package.lilpck"
            for name, content in (("data/secret.bin", freezer.ROM_MAGICS[0]), ("nested.bin", b"PK\x03\x04")):
                with zipfile.ZipFile(path, "w") as archive:
                    archive.writestr(name, content)
                with self.assertRaises(ValueError):
                    freezer.audit_zip(path, "test")
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("licenses/COPYING", "retained notice")
            self.assertEqual(len(freezer.audit_zip(path, "test")["members"]), 1)

    def test_inventory_comparison_detects_missing_and_changed_file(self):
        with self.assertRaises(ValueError):
            freezer.compare_inventory({"a": {"sha256": "one"}}, {"a": {"sha256": "two"}})
        with self.assertRaises(ValueError):
            freezer.compare_inventory({"a": {}}, {})

    def test_sticky_mode_is_retained(self):
        self.assertEqual(freezer.parse_manifest("D\t1777\ttmp\t\n")[0][1], "1777")

    def test_shipped_mips_checker_uses_dsp_capable_cpu(self):
        self.assertEqual(freezer.MIPS_RUNNER, ("qemu-mipsel", "-cpu", "74Kf"))

    def test_generated_large_file_is_explicitly_sparse(self):
        with tempfile.TemporaryDirectory(dir=freezer.DESTINATION) as temporary:
            path = Path(temporary) / "sparse.img"
            freezer.sparse_file(path, 2 * 1024**3)
            self.assertEqual(path.stat().st_size, 2 * 1024**3)
            if str(path).startswith("/mnt/c/"):
                native = "C:\\" + str(path)[7:].replace("/", "\\")
                allocated = freezer.run(["/mnt/c/Windows/System32/fsutil.exe", "sparse", "queryrange", native])
                self.assertFalse(allocated.strip())
            else:
                self.assertLess(path.stat().st_blocks * 512, 1024**2)

    @unittest.skipUnless(all(shutil.which(tool) for tool in ("gcc", "mkfs.fat", "mcopy", "fsck.fat")), "WSL FAT tools required")
    def test_shifted_growable_fat_file_only_integration(self):
        with tempfile.TemporaryDirectory(dir=freezer.DESTINATION) as temporary:
            work = Path(temporary)
            p1, p2 = work / "lilos.fat", work / "lilroms.fat"
            freezer.sparse_file(p1, 1024 * freezer.MIB)
            freezer.sparse_file(p2)
            freezer.run(["mkfs.fat", "-F", "32", "-S", "512", "-s", "8", "-R", "64", "-h", "2048", "-n", "LILOS", p1])
            freezer.normalize_os_fat(p1, 2097152)
            freezer.fat_geometry(p1, 2048, 2097152, "LILOS")
            freezer.run(["fsck.fat", "-n", p1])
            freezer.prepare_growable_fat(p2, 768, 2099200, 131072)
            tree = work / "roms-tree"
            (tree / "roms/snes").mkdir(parents=True)
            (tree / "roms/snes/sample.bin").write_bytes(bytes(range(256)) * 1024)
            freezer.run(["mcopy", "-s", "-i", p2, tree / "roms", "::/"])
            freezer.synchronize_fsinfo(p2)
            freezer.fat_geometry(p1, 2048, 2097152, "LILOS")
            freezer.fat_geometry(p2, 2099200, 1572864, "LILROMS")
            freezer.run(["fsck.fat", "-n", p2])
            prefix = bytearray(freezer.MIB)
            prefix[510:512] = b"\x55\xaa"
            prefix = freezer.new_prefix({"prefix": prefix}, 1024, 768)
            result = freezer.growth_probe(prefix, p1, p2, work)
            self.assertTrue(result["grow_to_4gib_passed"])
            self.assertTrue(result["retry_passed"])
            self.assertTrue(result["files_preserved"])
            self.assertTrue(result["oversize_refused_before_writes"])


if __name__ == "__main__":
    unittest.main()
