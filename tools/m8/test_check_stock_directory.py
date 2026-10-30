#!/usr/bin/env python3
"""Synthetic folder/report tests; never open or build a raw stock image."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_stock_directory as checker
import build_stock_candidate as builder


class StockDirectoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.card = self.root / "extracted"
        self.stock = self.card / "cubegm"
        self.stock.mkdir(parents=True)
        self.reference = self.root / "candidate06.json"
        self.provenance = "a" * 64
        for name in builder.BOOT_FILES + builder.STOCK_UI_FILES + (checker.VENDOR_LIBRARY,):
            self.put(name)
        self.hashes = {name: hashlib.sha256(("fixture:" + name).encode()).hexdigest()
                       for name in checker.HASH_FILES}
        self.report = dict(source_img_sha256=self.provenance,
                           stock_hashes={name: self.hashes[name] for name in builder.BOOT_FILES},
                           required_vendor_libs={checker.VENDOR_LIBRARY: self.hashes[checker.VENDOR_LIBRARY]})
        self.write_report()

    def tearDown(self):
        self.temp.cleanup()

    def put(self, name, content=None):
        path = self.stock / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if content is not None else ("fixture:" + name).encode())
        return path

    def write_report(self):
        self.reference.write_text(json.dumps(self.report), encoding="utf-8")

    def check(self):
        return checker.check_directory(self.card, self.reference)

    def test_checks_exactly_twelve_files_read_only(self):
        self.put("cores/game.sfc", b"synthetic nonselected sentinel")
        self.put("lib/gba_bios.bin", b"synthetic nonselected sentinel")
        self.put("lib/neogeo.zip", b"synthetic nonselected sentinel")
        before = {name: (self.stock / name).stat().st_mtime_ns for name in checker.HASH_FILES}
        with patch.object(checker, "file_hash", wraps=checker.file_hash) as hashed:
            result = self.check()
        self.assertTrue(result["passed"])
        self.assertEqual(result["checked_files"], 12)
        self.assertEqual(set(result["files"]), set(checker.HASH_FILES))
        self.assertEqual({call.args[0].relative_to(self.stock).as_posix() for call in hashed.call_args_list},
                         set(checker.HASH_FILES))
        self.assertEqual(before, {name: (self.stock / name).stat().st_mtime_ns for name in checker.HASH_FILES})
        self.assertFalse(result["raw_image_read"])
        self.assertFalse(result["rom_data_read"])
        self.assertTrue(result["raw_boot_gap_required_for_new_image"])

    def test_accepts_cubegm_directly(self):
        self.assertTrue(checker.check_directory(self.stock, self.reference)["passed"])

    def test_missing_boot_file_is_reported(self):
        (self.stock / "dtc.bin").unlink()
        result = self.check()
        self.assertFalse(result["passed"])
        self.assertIn("error", result["files"]["dtc.bin"])

    def test_corrupt_boot_file_is_reported(self):
        self.put("advapi32.dll", b"corrupt synthetic kernel")
        result = self.check()
        self.assertFalse(result["passed"])
        self.assertFalse(result["files"]["advapi32.dll"]["matches"])

    def test_required_vendor_library_is_checked(self):
        self.put(checker.VENDOR_LIBRARY, b"different vendor library")
        self.assertFalse(self.check()["files"][checker.VENDOR_LIBRARY]["matches"])

    def test_rejects_unexpected_report_paths(self):
        self.report["required_vendor_libs"]["../../roms/game.sfc"] = "b" * 64
        self.write_report()
        with self.assertRaises(ValueError):
            self.check()

    def test_rejects_missing_or_invalid_reference_hashes(self):
        self.report["stock_hashes"]["dtb.bin"] = "not-a-hash"
        self.write_report()
        with self.assertRaises(ValueError):
            self.check()
        del self.report["stock_hashes"]["dtb.bin"]
        self.write_report()
        with self.assertRaises(ValueError):
            self.check()

    def test_rejects_symlink_dependency_and_parent(self):
        outside = self.root / "outside"
        outside.write_bytes(b"not a selected stock file")
        (self.stock / "dtb.bin").unlink()
        (self.stock / "dtb.bin").symlink_to(outside)
        self.assertFalse(self.check()["files"]["dtb.bin"]["matches"])
        link = self.root / "linked-card"
        link.symlink_to(self.card, target_is_directory=True)
        with self.assertRaises(ValueError):
            checker.check_directory(link, self.reference)

    def test_rejects_oversize_dependency_without_hashing(self):
        path = self.stock / "dtb.bin"
        with path.open("r+b") as stream:
            stream.truncate(checker.MAX_HASH_BYTES + 1)
        with patch.object(checker.hashlib, "sha256", side_effect=AssertionError("must not hash")):
            with self.assertRaises(ValueError):
                checker.file_hash(path)

    def test_import_allowlist_without_mcopy_or_rom_reads(self):
        included = ("ui_pt.cpd", "lib/libc.so.6", "usr/lib/libSDL2.so",
                    "usr/lib32/libSDL2.so", "usr/lib/directfb-1.7-7-pure/inputdrivers/libinput.so",
                    "usr/lib/ts/linear.so", "usr/bin/dfbplay", "usr/share/xml/fonts.dtd")
        excluded = ("pagefile.sys", "menu.log", "saves/game.srm", "cores/game.sfc",
                    "lib/gba_bios.bin", "lib/neogeo.zip", "usr/bin/hcfota",
                    "usr/bin/unapproved", "usr/lib/game.sfc", "usr/share/roms/game.sfc",
                    "usr/share/xml/game.sfc")
        for name in included + excluded:
            self.put(name)
        output = self.root / "imported"
        with patch.object(builder, "run", side_effect=AssertionError("no mtools expected")), \
                patch.object(checker.shutil, "copyfile", wraps=checker.shutil.copyfile) as copied:
            hashes, result = builder.extract_stock_directory(self.card, output, self.reference, self.provenance)
        self.assertEqual(hashes, self.report["stock_hashes"])
        selected = {call.args[0].relative_to(self.stock).as_posix() for call in copied.call_args_list}
        self.assertTrue(set(included).issubset(selected))
        self.assertTrue(set(excluded).isdisjoint(selected))
        self.assertEqual(set(result["imported_files"]), selected)
        for name in checker.HASH_FILES:
            self.assertEqual(checker.file_hash(output / name)[0], self.hashes[name])

    def test_mismatch_fails_before_destination_creation(self):
        self.put("icube", b"modified entrypoint")
        destination = self.root / "must-not-exist"
        with self.assertRaises(ValueError):
            checker.copy_directory(self.card, destination, self.reference)
        self.assertFalse(destination.exists())

    def test_raw_provenance_must_match_reference(self):
        destination = self.root / "must-not-exist"
        with self.assertRaises(ValueError):
            checker.copy_directory(self.card, destination, self.reference, "c" * 64)
        self.assertFalse(destination.exists())

    def test_never_imports_into_source_or_existing_destination(self):
        for destination in (self.stock, self.stock / "new", self.root):
            with self.subTest(destination=destination), self.assertRaises(ValueError):
                checker.copy_directory(self.card, destination, self.reference)

    def test_cli_status_and_json(self):
        arguments = ["checker", "--stock-directory", str(self.card), "--candidate-report", str(self.reference)]
        output = io.StringIO()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(output):
            self.assertEqual(checker.main(), 0)
        self.assertEqual(json.loads(output.getvalue())["checked_files"], 12)
        (self.stock / "icube").unlink()
        with patch("sys.argv", arguments), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(checker.main(), 1)

    def test_builder_still_requires_raw_stock_image(self):
        with patch("sys.argv", ["builder", "--stock-directory", str(self.card), "--build", "unused",
                                "--output", "unused.img", "--experimental"]), \
                contextlib.redirect_stderr(io.StringIO()), \
                patch.object(builder, "sha", side_effect=AssertionError("must not hash raw image")):
            with self.assertRaises(SystemExit) as error:
                builder.main()
        self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
