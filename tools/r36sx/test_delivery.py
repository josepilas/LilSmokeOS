#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from verify_delivery import validate


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.image = self.root / "fixture.img"
        self.image.write_bytes(b"authored image fixture")
        self.report = dict(image_generated=True, protected_files_unchanged=True,
                           image_readback_protected_sha256=True, physical_boot_verified=False,
                           diagnostic_revision=2, profile="r36sx-2.7-owner-backup-20261022",
                           image_bytes=self.image.stat().st_size,
                           image_sha256=hashlib.sha256(self.image.read_bytes()).hexdigest())
        self.server = dict(success=True, id="fixture1", name=self.image.name,
                           size=self.report["image_bytes"], hash_sha256=self.report["image_sha256"])
        self.scope = patch("verify_delivery.RELEASE", self.root)
        self.scope.start()

    def tearDown(self):
        self.scope.stop()
        self.temp.cleanup()

    def test_matching_upload_keeps_file_during_validation(self):
        validate(self.image, self.report, self.server, "fixture1")
        self.assertTrue(self.image.exists())

    def test_bad_metadata_never_deletes(self):
        for key, value in (("size", 0), ("name", "other.img"), ("hash_sha256", "0" * 64),
                           ("id", "otherid1"), ("success", False)):
            with self.assertRaises(ValueError):
                validate(self.image, self.report, dict(self.server, **{key: value}), "fixture1")
            self.assertTrue(self.image.exists())

    def test_same_size_local_mutation_rejected(self):
        self.image.write_bytes(b"x" * self.image.stat().st_size)
        with self.assertRaises(ValueError):
            validate(self.image, self.report, self.server, "fixture1")
        self.assertTrue(self.image.exists())

    def test_scope_and_symlinks_rejected(self):
        link = self.root / "alias.img"
        link.symlink_to(self.image)
        with self.assertRaises(ValueError):
            validate(link, self.report, self.server, "fixture1")
        with patch("verify_delivery.RELEASE", self.root / "another"):
            with self.assertRaises(ValueError):
                validate(self.image, self.report, self.server, "fixture1")


if __name__ == "__main__":
    unittest.main()
