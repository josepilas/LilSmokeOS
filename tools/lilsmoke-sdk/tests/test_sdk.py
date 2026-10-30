# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Pure stdlib host tests. Run on Windows or POSIX; no toolchain required."""
import contextlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lilsmoke_sdk as sdk


class SDKTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lilsmoke-sdk-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.project = self.base / "project"
        sdk.new_project(self.project, "org.example.note", "Developer Note")
        self.payload = self.project / "payload"

    def write_meta(self, **fields):
        meta = json.loads((self.payload / "meta.json").read_text(encoding="utf-8"))
        meta.update(fields)
        (self.payload / "meta.json").write_bytes(sdk.json_bytes(meta))

    def archive(self, extra=None, meta=None):
        path = self.base / "hostile.lilpck"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("meta.json", sdk.json_bytes(meta or dict(
                id="org.example.note", name="Developer Note", version="1.0")))
            archive.writestr("run.sh", b"#!/bin/sh\nexit 0\n")
            for name, data in (extra or {}).items():
                archive.writestr(name, data)
        # Windows ZipInfo rewrites backslashes while writing; retain a truly
        # hostile raw ZIP filename for the cross-platform reader test.
        if os.name == "nt":
            data = path.read_bytes()
            for name in (extra or {}):
                if "\\" in name:
                    data = data.replace(name.replace("\\", "/").encode(), name.encode())
            path.write_bytes(data)
        return path

    def test_scaffold_and_real_shell_contract(self):
        report = sdk.validate(self.project)
        self.assertEqual(report["launcher"], "run.sh")
        self.assertEqual(report["metadata"]["id"], "org.example.note")
        script = (self.payload / "run.sh").read_text(encoding="utf-8")
        self.assertIn("/usr/bin/lilsmoke-keyboard", script)
        self.assertNotIn("eval ", script)
        self.assertTrue((self.payload / "LICENSE").is_file())
        self.assertFalse((self.payload / "userdata").exists())

    def test_deterministic_zip_and_host_source_excluded(self):
        (self.project / "private-host-sentinel.txt").write_text("test-only excluded sentinel")
        first, second = self.base / "one.lilpck", self.base / "two.lilpck"
        sdk.package(self.project, first)
        sdk.package(self.project, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            self.assertNotIn("private-host-sentinel.txt", archive.namelist())
            self.assertNotIn("sdk.json", archive.namelist())
            self.assertIn("run.sh", archive.namelist())
        with self.assertRaises(FileExistsError):
            sdk.package(self.project, first)

    def test_runtime_bundle_and_nested_scripts(self):
        runtime = self.base / "runtime"
        (runtime / "lib").mkdir(parents=True)
        (runtime / "lib/placeholder.txt").write_bytes(b"test-only runtime data")
        (runtime / "worker.sh").write_bytes(b"#!/bin/sh\nexit 0\n")
        package = self.base / "runtime.lilpck"
        sdk.package(self.project, package, runtime)
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(archive.read("runtime/lib/placeholder.txt"), b"test-only runtime data")
            self.assertIn("runtime/worker.sh", archive.namelist())
        self.assertEqual(sdk.validate(package)["launcher"], "run.sh")

    def test_runtime_collision(self):
        runtime = self.base / "runtime"
        runtime.mkdir()
        (runtime / "x").write_bytes(b"second")
        (self.payload / "runtime").mkdir()
        (self.payload / "runtime/x").write_bytes(b"first")
        with self.assertRaises(sdk.SDKError):
            sdk.validate(self.project, runtime)

    def test_output_inside_payload_refused(self):
        with self.assertRaises(sdk.SDKError):
            sdk.package(self.project, self.payload / "recursive.lilpck")
        self.assertFalse((self.payload / "recursive.lilpck").exists())

    def test_native_requires_real_build(self):
        for template in ("native-c", "native-cpp", "sdl2-c"):
            project = self.base / template
            sdk.new_project(project, "org.example.native", "Native", template=template)
            with self.assertRaisesRegex(sdk.SDKError, "Build the native"):
                sdk.validate(project)
            self.assertTrue((project / "include/lilsmoke/app.h").is_file())
            binary = project / "payload/runtime/bin/application"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"test-only not an executable placeholder")
            with self.assertRaisesRegex(sdk.SDKError, "actual target ELF"):
                sdk.validate(project)
        with self.assertRaises(sdk.SDKError):
            sdk.build(self.project, None)

    def test_arbitrary_launcher_name(self):
        project=self.base/"custom"
        sdk.new_project(project,"org.example.custom","Custom",launcher="SDK Studio.SH")
        self.assertEqual(sdk.validate(project)["launcher"],"SDK Studio.SH")
        self.assertFalse((project/"payload/run.sh").exists())
        (project/"payload/runtime").mkdir()
        (project/"payload/runtime/extra.sh").write_bytes(b"#!/bin/sh\nexit 0\n")
        meta=sdk.parse_json((project/"payload/meta.json").read_bytes())
        meta["entrypoint"]="not-the-launcher.sh"
        (project/"payload/meta.json").write_bytes(sdk.json_bytes(meta))
        package=self.base/"custom.lilpck"
        sdk.package(project,package)
        self.assertEqual(sdk.validate(package)["launcher"],"SDK Studio.SH")
        (project/"payload/another.sh").write_bytes(b"#!/bin/sh\n")
        with self.assertRaises(sdk.SDKError): sdk.validate(project)

    def test_launcher_fat_and_extension_edges(self):
        for name in (".sh",".SH"," .SH","a/b.sh","../x.sh","CON.sh","not-shell.txt","x.sh ","..sh"):
            with self.subTest(name=name),self.assertRaises(sdk.SDKError):
                sdk.new_project(self.base/"bad-launcher","org.example.bad","Bad",launcher=name)
        self.assertFalse((self.base/"bad-launcher").exists())
        hidden=self.base/"hidden"
        sdk.new_project(hidden,"org.example.hidden","Hidden",launcher=".hidden.sh")
        self.assertEqual(sdk.validate(hidden)["launcher"],".hidden.sh")
        for name in (".sh",".SH"):
            (self.payload/"run.sh").rename(self.payload/name)
            with self.assertRaises(sdk.SDKError): sdk.validate(self.project)
            (self.payload/name).rename(self.payload/"run.sh")

    def test_sdl_scaffold_is_source_only_until_target_build(self):
        project=self.base/"sdl"
        sdk.new_project(project,"org.example.studio","Studio",template="sdl2-c",launcher="Studio.sh")
        self.assertTrue((project/"include/lilsmoke/sdl.h").is_file())
        self.assertTrue((project/"include/lilsmoke/lifecycle.h").is_file())
        self.assertTrue((project/"src/lilsmoke_sdl.c").is_file())
        script=(project/"payload/Studio.sh").read_text()
        self.assertIn("LILSMOKE_AUDIO_OVERRIDE",script)
        self.assertNotIn("LD_PRELOAD=",script)
        self.assertNotIn("INPUT_EXTERNAL",script)
        with self.assertRaises(sdk.SDKError): sdk.validate(project)

    def test_root_script_ambiguity_case_insensitive(self):
        (self.payload / "second.SH").write_bytes(b"#!/bin/sh\n")
        with self.assertRaisesRegex(sdk.SDKError, "Exactly one"):
            sdk.validate(self.project)
        (self.payload / "second.SH").unlink()
        (self.payload / "run.sh").unlink()
        with self.assertRaises(sdk.SDKError):
            sdk.validate(self.project)

    def test_invalid_script_bytes(self):
        for data in (b"#!/bin/sh\r\nexit 0\r\n", b"#!/bin/bash\n", b"#!/bin/sh\n\0", b"#!/bin/sh\n\xff"):
            with self.subTest(data=data):
                (self.payload / "run.sh").write_bytes(data)
                with self.assertRaises((sdk.SDKError, UnicodeError)):
                    sdk.validate(self.project)

    def test_safe_metadata(self):
        for field, values in {
            "name": ["../bad", "CON", "nul.txt", "Trailing.", ".hidden", "x" * 121, "a\0b", "a:b"],
            "id": ["", "a..b", "../id", "id/name", "x" * 129, None],
            "version": ["", "v1", "1.2.3.4", "1-", "1+", "1+bad/field", None],
            "architecture": ["x86_64", "arm", None],
            "open_core": [None, [], ["gba", 3], ["unknown"], ["gba"] * 26],
            "icon": ["../icon.png", "/icon.png", None],
        }.items():
            for value in values:
                with self.subTest(field=field, value=value):
                    with self.assertRaises(sdk.SDKError):
                        sdk.metadata(sdk.json_bytes({**dict(id="org.example.app", name="App", version="1"), field: value}))

    def test_icon_missing_and_oversized(self):
        self.write_meta(icon="missing.png")
        with self.assertRaises(sdk.SDKError):
            sdk.validate(self.project)
        (self.payload / "missing.png").write_bytes(b"not a decoded image")
        self.assertTrue(sdk.validate(self.project)["valid"])
        # Size/reference checks are not a claim of image decode validation.
        with (self.payload / "missing.png").open("wb") as stream:
            stream.truncate(2 * 1024 * 1024 + 1)
        with self.assertRaises(sdk.SDKError):
            sdk.validate(self.project)

    def test_version_and_unknown_fields_preserved(self):
        for version in ("1", "1.2", "1.2.3", "001.02.3-rc.1+build.7"):
            meta = sdk.metadata(sdk.json_bytes(dict(
                id="org.example.app", name="App", version=version, custom={"retained": [1, True]})))
            self.assertEqual(meta["custom"], {"retained": [1, True]})

    def test_open_core_alias_normalization(self):
        self.write_meta(open_core=["GBA", "gbadvance", "GENESIS", "ps1"])
        report = sdk.validate(self.project)
        self.assertEqual(report["metadata"]["open_core"], ["gba", "md", "psx"])
        archive = self.base / "normalized.lilpck"
        sdk.package(self.project, archive)
        with zipfile.ZipFile(archive) as source:
            self.assertEqual(json.loads(source.read("meta.json"))["open_core"], ["gba", "md", "psx"])

    def test_bad_json(self):
        for data in (b'{"id":"a","id":"b"}', b'{"x":NaN}', b'{"x":1e9999}', b'\xff',
                     b'{"x":"\\u0000"}', b'{}garbage', b"[" * 30 + b"0" + b"]" * 30):
            with self.subTest(data=data):
                with self.assertRaises(sdk.SDKError):
                    sdk.parse_json(data)
        with self.assertRaises(sdk.SDKError):
            sdk.parse_json(b" " * (sdk.MAX_META + 1))

    def test_archive_paths_and_case_collisions(self):
        for extra in ({"../escaped": b"x"}, {"/absolute": b"x"}, {"C:/bad": b"x"},
                      {"x\\y": b"x"}, {"./dot": b"x"}, {"double//slash": b"x"},
                      {"DATA/a": b"x", "data/b": b"x"}, {"file": b"x", "file/child": b"x"},
                      {"runtime/.lilsmoke-install.json": b"x"}, {".env": b"fake-only"},
                      {".git/config": b"fake-only"}, {"a/CON": b"x"}):
            with self.subTest(extra=extra):
                with self.assertRaises(sdk.SDKError):
                    sdk.validate(self.archive(extra))
        self.assertFalse((self.base / "escaped").exists())

    def test_duplicate_zip_names(self):
        path = self.archive()
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            with zipfile.ZipFile(path, "a") as archive:
                archive.writestr("meta.json", b"{}")
        with self.assertRaises(sdk.SDKError):
            sdk.validate(path)

    def test_wrapper_folder_and_meta_case(self):
        for name in ("folder/meta.json", "META.JSON"):
            path = self.base / "wrapped.lilpck"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr(name, b"{}")
                archive.writestr("folder/run.sh", b"#!/bin/sh\n")
            with self.assertRaises(sdk.SDKError):
                sdk.validate(path)

    def test_symlink_zip(self):
        path = self.archive()
        info = zipfile.ZipInfo("runtime/linked")
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(path, "a") as archive:
            archive.writestr(info, "../outside")
        with self.assertRaises(sdk.SDKError):
            sdk.validate(path)

    def test_directory_links(self):
        link = self.payload / "link"
        try:
            os.symlink(self.project / "sdk.json", link)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit symlink creation")
        with self.assertRaises(sdk.SDKError):
            sdk.validate(self.project)

    def test_bounds_without_large_fixtures(self):
        for constant, limit in (("MAX_ENTRIES", 1), ("MAX_TOTAL", 10), ("MAX_FILE", 5), ("MAX_SCRIPT", 5)):
            with self.subTest(constant=constant), mock.patch.object(sdk, constant, limit):
                with self.assertRaises(sdk.SDKError):
                    sdk.validate(self.project)
        with self.assertRaises(sdk.SDKError):
            sdk.member_name("x/" * 33 + "file")
        with self.assertRaises(sdk.SDKError):
            sdk.member_name("x" * 1025)

    def test_zip_crc_and_truncation(self):
        path = self.archive()
        data = path.read_bytes()
        path.write_bytes(data[:-10])
        with self.assertRaises(sdk.SDKError):
            sdk.validate(path)
        path = self.base / "crc.lilpck"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
            archive.writestr("meta.json", sdk.json_bytes(dict(id="a", name="App", version="1")))
            archive.writestr("run.sh", b"#!/bin/sh\n#CRC_SENTINEL\n")
        data = path.read_bytes().replace(b"CRC_SENTINEL", b"CRC_BROKEN!!")
        path.write_bytes(data)
        with self.assertRaises(zipfile.BadZipFile):
            sdk.validate(path)

    def test_elf_identity_not_fake_execution(self):
        header = bytearray(52)
        header[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHIIIIIHHHHHH", header, 16, 2, 8, 1, 0, 0, 0, 0x70001007, 52, 32, 0, 0, 0, 0)
        self.assertTrue(sdk.elf_check(header, "structural-fixture"))
        for index, value in ((4, 2), (5, 2), (18, 62)):
            bad = bytearray(header)
            bad[index] = value
            with self.assertRaises(sdk.SDKError):
                    sdk.elf_check(bad, "structural-fixture")

    def test_cli_errors_and_existing_project_preserved(self):
        with contextlib.redirect_stderr(io.StringIO()) as error:
            code = sdk.main(["new", str(self.project), "--id", "org.example.keep", "--name", "Keep"])
        self.assertEqual(code, 1)
        self.assertFalse(json.loads(error.getvalue())["valid"])
        self.assertEqual(sdk.validate(self.project)["metadata"]["name"], "Developer Note")

    def test_project_spec_malformed(self):
        for data in (b"[]", b'{"format":2,"template":"shell"}', b'{"format":1,"template":"fake"}'):
            (self.project / "sdk.json").write_bytes(data)
            with self.assertRaises(sdk.SDKError):
                sdk.validate(self.project)

    def test_existing_helper_requires_explicit_sdk(self):
        output = self.base / "must-not-be-created"
        result = subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("integration.py")),
                                 "--out", str(output), "--existing-mips-helper", str(self.base / "offline-helper")],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--existing-mips-helper requires --sdk", result.stderr)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
