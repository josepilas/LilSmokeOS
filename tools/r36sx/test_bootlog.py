#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import os
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib
from boot_report import decode_journal, inspect_folder, regular


class JournalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.journal = self.root / "journal.bin"
        self.journal.write_bytes(bytes(16384))

    def tearDown(self):
        self.temp.cleanup()

    def record(self, seq, event="TEST"):
        data = bytearray(512)
        data[:8] = b"LSBJ0001"
        struct.pack_into("<4I", data, 8, seq, 123, 4, len(event))
        data[24:24 + len(event)] = event.encode("ascii")
        struct.pack_into("<I", data, 508, zlib.crc32(data[:508]))
        return data

    def test_blank_is_not_a_claim_of_kernel_failure(self):
        self.assertIsNone(decode_journal(self.journal.read_bytes())["last_checkpoint"])

    def test_sorted_records_and_torn_write(self):
        data = self.record(5) + self.record(2) + bytearray(512 * 30)
        data[512 + 27] ^= 1
        decoded = decode_journal(data)
        self.assertEqual([r["sequence"] for r in decoded["records"]], [5])
        self.assertEqual(decoded["invalid_slots"], [1])

    def test_crc_and_length_validation(self):
        data = self.record(1)
        struct.pack_into("<I", data, 20, 480)
        struct.pack_into("<I", data, 508, zlib.crc32(data[:508]))
        self.assertEqual(decode_journal(data + bytes(512 * 31))["invalid_slots"], [0])
        with self.assertRaises(ValueError):
            decode_journal(bytes(1))

    def test_symlink_refused(self):
        target = self.root / "link"
        target.symlink_to(self.journal)
        with self.assertRaises(ValueError):
            regular(self.root, "link")

    def test_folder_diagnosis_checks_stock_without_claiming_a_boot(self):
        (self.root / "lilsmoke").mkdir()
        (self.root / "cubegm").mkdir()
        (self.root / "rootfs/usr/bin").mkdir(parents=True)
        protected = {}
        for name in ("cubegm/kernel.test", "rootfs/usr/bin/cubevol"):
            (self.root / name).write_bytes(b"authored fixture")
            protected[name] = hashlib.sha256(b"authored fixture").hexdigest()
        (self.root / "lilsmoke/boot-manifest.json").write_text(json.dumps({"profile": "fixture", "protected": protected}))
        (self.root / "lilsmoke/boot-journal.bin").write_bytes(bytes(16384))
        report = inspect_folder(self.root)
        self.assertFalse(report["physical_boot_verified"])
        self.assertEqual(report["protected_mismatches"], [])
        self.assertIn("does not prove", report["diagnosis"])
        (self.root / "cubegm/kernel.test").write_bytes(b"changed fixture")
        self.assertEqual(inspect_folder(self.root)["protected_mismatches"], ["cubegm/kernel.test"])
        with self.assertRaises(ValueError):
            regular(self.root, "cubegm/../../outside")

    @unittest.skipUnless(os.environ.get("R36_BOOTLOG_COMMAND"), "set command to exercise the real C writer")
    def test_real_writer_ring_no_growth_and_recovery(self):
        import shlex
        command = shlex.split(os.environ["R36_BOOTLOG_COMMAND"])
        def write(path, event, success=True):
            result = subprocess.run(command + ["--journal", str(path), event], capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0 if success else 1, result.stderr.decode())
        for i in range(35):
            write(self.journal, f"EVENT_{i}")
        self.assertEqual(self.journal.stat().st_size, 16384)
        decoded = decode_journal(self.journal.read_bytes())
        self.assertEqual([r["sequence"] for r in decoded["records"]], list(range(4, 36)))
        self.assertEqual(decoded["last_checkpoint"], "EVENT_34")
        data = bytearray(self.journal.read_bytes())
        data[2 * 512 + 100] ^= 1  # newest record interrupted
        self.journal.write_bytes(data)
        write(self.journal, "RECOVERED")
        self.assertEqual(decode_journal(self.journal.read_bytes())["last_checkpoint"], "RECOVERED")
        missing = self.root / "missing"
        write(missing, "NO_CREATE", False)
        self.assertFalse(missing.exists())
        self.journal.write_bytes(bytes(512))
        write(self.journal, "NO_EXTEND", False)
        self.assertEqual(self.journal.stat().st_size, 512)
        self.journal.write_bytes(bytes(16384))
        link = self.root / "alias"
        link.symlink_to(self.journal)
        write(link, "NO_LINK", False)
        self.assertEqual(self.journal.read_bytes(), bytes(16384))


if __name__ == "__main__":
    unittest.main()
