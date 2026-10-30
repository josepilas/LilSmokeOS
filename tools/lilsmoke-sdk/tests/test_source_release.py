# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Standalone, pure stdlib tests for the source-only release boundary."""
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lilsmoke_sdk as sdk
import source_release as release


class SourceReleaseTests(unittest.TestCase):
    def test_deterministic_manifest_links_and_exclusions(self):
        with tempfile.TemporaryDirectory(prefix="sdk-source-test-") as folder:
            root = Path(folder)
            source = root / "source"
            for name in release.FILES:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(sdk.HERE / name, path)
            sentinel = b"\0excluded-test-sentinel\xff"
            for name in ("_build/private-engine", "__pycache__/secret.pyc", "private-OS.cpp", "old.lilpck"):
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(sentinel)
            a, b = root / "a.zip", root / "b.zip"
            report = release.make_release(a, source)
            release.make_release(b, source)
            self.assertTrue(report["source_only"])
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as archive:
                self.assertEqual(set(archive.namelist()),
                                 {release.ROOT + n for n in (*release.FILES, "SHA256SUMS")})
                self.assertNotIn(sentinel, b"".join(map(archive.read, archive.namelist())))
            with self.assertRaises(FileExistsError):
                release.make_release(a, source)

    def test_docs_cannot_escape_or_link_missing_file(self):
        for target in ("../private.md", "missing.md", "/absolute.md"):
            with self.subTest(target=target), self.assertRaises(sdk.SDKError):
                release.check_links({"README.md": ("[bad](" + target + ")").encode()})

    def test_manifest_tampering_and_extra_members_rejected(self):
        with tempfile.TemporaryDirectory(prefix="sdk-source-tamper-") as folder:
            path = Path(folder) / "release.zip"
            release.make_release(path)
            with zipfile.ZipFile(path) as archive:
                saved = [(item, archive.read(item)) for item in archive.infolist()]
            altered = Path(folder) / "altered.zip"
            with zipfile.ZipFile(altered, "w") as archive:
                for item, data in saved:
                    archive.writestr(item, data + b"tampered" if item.filename.endswith("README.md") else data)
            with self.assertRaisesRegex(sdk.SDKError, "manifest mismatch"):
                release.verify_archive(altered)
            with zipfile.ZipFile(path, "a") as archive:
                archive.writestr("lilsmoke-sdk/private-runtime", b"not source")
            with self.assertRaises(sdk.SDKError):
                release.verify_archive(path)
            with self.assertRaises(sdk.SDKError):
                release.check_links({"README.md": b"[guide](docs/guide.md)"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
