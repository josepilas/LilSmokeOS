# SPDX-License-Identifier: MPL-2.0
"""Exercise build-wrapper routing with isolated, network-free command fixtures."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {
    "qemu-mipsel": "lilsmoke_qemu_mipsel_defconfig",
    "sf3000": "lilsmoke_sf3000_defconfig",
    "gb350": "lilsmoke_gb350_defconfig",
    "vbox-x86_64": "lilsmoke_vbox_x86_64_defconfig",
    "vbox-test": "lilsmoke_vbox_test_defconfig",
}


@unittest.skipIf(os.name == "nt" or not shutil.which("bash"), "Linux/WSL bash required")
class BuildWrapperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="lilsmoke-build-wrapper-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.tree = self.work / "external tree"
        for name in ("scripts/build.sh", "scripts/check-real-hardware-bsp.sh",
                     "board/sf3000/bsp-status.conf", "board/gb350/bsp-status.conf",
                     "configs/lilsmoke_media.fragment"):
            self.copy(name)
        for name in TARGETS.values():
            self.copy("configs/" + name)
        self.bin = self.work / "commands"
        self.bin.mkdir()
        self.log = self.work / "calls.jsonl"
        command = """#!/usr/bin/env python3
import json, os, pathlib, sys
with open(os.environ['TEST_CALLS'], 'a') as stream:
    stream.write(json.dumps([pathlib.Path(sys.argv[0]).name] + sys.argv[1:]) + '\\n')
if pathlib.Path(sys.argv[0]).name == 'git':
    destination = pathlib.Path(sys.argv[-1])
    destination.mkdir(parents=True)
    (destination / 'Makefile').write_text('# command fixture, not Buildroot\\n')
"""
        for name in ("make", "git"):
            path = self.bin / name
            path.write_text(command)
            path.chmod(0o755)
        self.buildroot = self.work / "buildroot source"
        self.buildroot.mkdir()
        (self.buildroot / "Makefile").write_text("# isolated command fixture\n")
        self.output = self.work / "separate output"
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        TEST_CALLS=str(self.log), LILSMOKE_BUILDROOT_DIR=str(self.buildroot),
                        LILSMOKE_OUTPUT_DIR=str(self.output), LILSMOKE_JOBS="2",
                        LILSMOKE_ALLOW_UNVERIFIED_BSP="0")

    def copy(self, name):
        target = self.tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)

    def run_wrapper(self, *args):
        return subprocess.run(["bash", str(self.tree / "scripts/build.sh"), *args],
                              cwd=self.work, env=self.env, text=True, capture_output=True,
                              timeout=30)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_lists_only_real_target_profiles_without_network(self):
        result = self.run_wrapper("--list-targets")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(result.stdout.split()), set(TARGETS))
        self.assertEqual(self.calls(), [])

    def test_unknown_target_fails_before_clone(self):
        self.env["LILSMOKE_BUILDROOT_DIR"] = str(self.work / "not-cloned")
        result = self.run_wrapper("no-such-target")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("available targets", result.stderr)
        self.assertNotIn("media.fragment", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_missing_profile_is_not_silently_replaced(self):
        (self.tree / "configs" / TARGETS["qemu-mipsel"]).unlink()
        result = self.run_wrapper("qemu-mipsel")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_all_five_profiles_configure_without_building_packages(self):
        for target, name in TARGETS.items():
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                result = self.run_wrapper(target, "--configure-only")
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = self.calls()
                self.assertEqual(len(calls), 1)
                self.assertEqual(calls[0][-1], name)
                self.assertIn("BR2_EXTERNAL=" + str(self.tree), calls[0])
                self.assertIn("O=" + str(self.output), calls[0])

    def test_default_build_loads_qemu_then_builds(self):
        result = self.run_wrapper()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][-1], TARGETS["qemu-mipsel"])
        self.assertEqual(calls[1][-1], "-j2")

    def test_hardware_builds_retain_bsp_gate(self):
        for target in ("sf3000", "gb350"):
            with self.subTest(target=target):
                result = self.run_wrapper(target)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("BSP is incomplete", result.stderr)
                self.assertEqual(self.calls(), [])

    def test_explicit_development_bypass_is_still_warned(self):
        self.env["LILSMOKE_ALLOW_UNVERIFIED_BSP"] = "1"
        result = self.run_wrapper("sf3000")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WARNING", result.stdout)
        self.assertEqual(len(self.calls()), 2)

    def test_clone_uses_pinned_tag_and_selected_path(self):
        clone = self.work / "new buildroot"
        self.env["LILSMOKE_BUILDROOT_DIR"] = str(clone)
        result = self.run_wrapper("qemu-mipsel", "--configure-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual(calls[0][:4], ["git", "clone", "--branch", "2024.02.3"])
        self.assertEqual(calls[0][-1], str(clone))
        self.assertEqual(len(calls), 2)

    def test_menuconfig_and_clean_use_selected_output(self):
        for target in ("menuconfig", "clean"):
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                result = self.run_wrapper(target)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.calls()[0][-1], target)
                self.assertIn("O=" + str(self.output), self.calls()[0])

    def test_bad_arguments_do_not_run_buildroot(self):
        for args in (("qemu-mipsel", "--wrong"), ("clean", "--configure-only"),
                     ("qemu-mipsel", "--configure-only", "extra"), ("../bad",)):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_wrapper(*args).returncode, 0)
                self.assertEqual(self.calls(), [])

    def test_invalid_jobs_never_invokes_package_build(self):
        self.env["LILSMOKE_JOBS"] = "0"
        result = self.run_wrapper("qemu-mipsel")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.calls()), 1)
        self.assertIn("positive integer", result.stderr)


if __name__ == "__main__":
    unittest.main()
