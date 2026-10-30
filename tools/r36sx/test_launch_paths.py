#!/usr/bin/env python3
"""Run production shell entry/early failures in isolated host chroots, no devices."""
# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from boot_report import decode_journal

PROJECT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get("R36_BOOTLOG_NATIVE_BIN") and getattr(os, "geteuid", lambda: -1)() == 0,
                     "requires a native helper and isolated-chroot capability")
class LaunchPaths(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="r36-launch-fixture-")
        self.root = Path(self.temp.name)
        self.base = self.root / "mnt/sdcard/lilsmoke"
        self.base.mkdir(parents=True)
        for path in ("tmp", "bin", "dev", "proc", "mnt/sdcard/cubegm"):
            (self.root / path).mkdir(parents=True, exist_ok=True)
        (self.root / "dev/null").touch()
        self.install_binary(Path("/bin/busybox"), "/bin/busybox")
        self.install_binary(Path(os.environ["R36_BOOTLOG_NATIVE_BIN"]), "/mnt/sdcard/lilsmoke/r36-bootlog")
        for name in ("sh", "cp", "chmod", "cat", "mkdir", "tail", "date", "dmesg", "rm", "rmdir"):
            (self.root / "bin" / name).symlink_to("busybox")
        (self.base / "boot-journal.bin").write_bytes(bytes(16384))
        source = PROJECT / "board/r36sx-2.7-stock"
        shutil.copyfile(source / "r36-launch.sh", self.base / "r36-launch.sh")
        shutil.copyfile(source / "r36-entry.sh", self.root / "mnt/sdcard/cubegm/icube.sh")

    def install_binary(self, source, destination):
        target = self.root / destination.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o755)
        result = subprocess.run(["ldd", str(source)], text=True, capture_output=True)
        for library in re.findall(r"/[^\s()]+", result.stdout):
            file = Path(library)
            if file.is_file():
                dest = self.root / library.lstrip("/")
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, dest)
                dest.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def run_entry(self, expected, env=None):
        result = subprocess.run(["chroot", str(self.root), "/bin/sh", "/mnt/sdcard/cubegm/icube.sh"],
                                env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", **(env or {})},
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return [row["event"] for row in decode_journal((self.base / "boot-journal.bin").read_bytes())["records"]]

    def test_entry_ram_copy_and_early_probe_failure(self):
        events = self.run_entry(70)
        self.assertEqual(events[:4], ["SHELL_HOOK:/mnt/sdcard/cubegm/icube.sh", "SHELL_ENTRY", "RAM_SHELL", "LAUNCH_LOCKED"])
        self.assertEqual(events[-1], "LAUNCH_FAILED:not an enabled handheld panel")
        self.assertFalse((self.root / "tmp/lilsmoke-m8-launch.lock").exists())
        self.assertIn("R36_LAUNCH_FAILED=", (self.base / "bringup.log").read_text())

    def test_competing_entry_does_not_touch_active_script_or_log(self):
        lock = self.root / "tmp/lilsmoke-m8-launch.lock"
        lock.mkdir()
        (lock / "owner").write_text("999\n")
        (self.base / "bringup.log").write_text("running log")
        ram = self.root / "tmp/lilsmoke-r36-launch.sh"
        ram.write_text("running script")
        events = self.run_entry(73)
        self.assertEqual(events[-1], "LOCK_BUSY")
        self.assertEqual(ram.read_text(), "running script")
        self.assertEqual((self.base / "bringup.log").read_text(), "running log")
        self.assertEqual((lock / "owner").read_text(), "999\n")

    def test_copy_failure_is_persisted_and_lock_removed(self):
        (self.root / "tmp/lilsmoke-r36-launch.sh").symlink_to("/missing-parent/file")
        events = self.run_entry(70)
        self.assertEqual(events[-1], "EARLY_FAIL:RAM script copy")
        self.assertFalse((self.root / "tmp/lilsmoke-m8-launch.lock").exists())


if __name__ == "__main__":
    unittest.main()
