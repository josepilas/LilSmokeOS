# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Structural ELF fixtures are parser tests, never execution evidence."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lilsmoke_sdk as sdk
import runtime_audit
from runtime_audit import check_runtime, elf_info, identity_errors, base_file, ELFError
from preflight import doctor


def elf(needed=(), soname=None, interpreter=None, order="<", flags=0x70001007, fp=1, abi=True, runpath=None):
    data = bytearray(1024)
    data[:7] = b"\x7fELF\x01" + (b"\x01" if order == "<" else b"\x02") + b"\x01"
    ph = [(1, 0, 0x10000, 0, len(data), len(data), 5, 16)]
    if abi:
        ph.append((0x70000003, 256, 0x10100, 0, 24, 24, 4, 8))
        struct.pack_into(order + "H6B4I", data, 256, 0, 32, 2, 1, 1, 0, fp, 0, 3, 1, 0)
    if interpreter:
        raw = interpreter.encode() + b"\0"
        data[288:288 + len(raw)] = raw
        ph.append((3, 288, 0x10120, 0, len(raw), len(raw), 4, 1))
    strings = bytearray(b"\0")
    dynamic = []
    for tag, values in ((1, needed), (14, [soname] if soname else []), (29, [runpath] if runpath else [])):
        for value in values:
            dynamic.append((tag, len(strings)))
            strings.extend(value.encode() + b"\0")
    if dynamic:
        dynamic += [(5, 0x10280), (10, len(strings)), (0, 0)]
        data[640:640 + len(strings)] = strings
        for i, pair in enumerate(dynamic):
            struct.pack_into(order + "iI", data, 384 + i * 8, *pair)
        ph.append((2, 384, 0x10180, 0, len(dynamic) * 8, len(dynamic) * 8, 4, 4))
    struct.pack_into(order + "HHIIIIIHHHHHH", data, 16, 3, 8, 1, 0, 52, 0, flags, 52, 32, len(ph), 0, 0, 0)
    for i, values in enumerate(ph):
        struct.pack_into(order + "8I", data, 52 + i * 32, *values)
    return bytes(data)


class RuntimeAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sdk-audit-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "project"
        sdk.new_project(self.project, "org.example.audit", "Audit", launcher="Any Launcher.SH")
        self.payload = self.project / "payload"
        self.base = self.root / "base"
        self.base.mkdir()

    def write(self, name, data, base=False):
        path = (self.base if base else self.payload) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def codes(self, report):
        return {x["code"] for x in report["errors"]}

    def test_static_and_arbitrary_launcher_read_only(self):
        self.write("runtime/bin/app", elf())
        before = {str(p): p.read_bytes() for p in self.project.rglob("*") if p.is_file()}
        report = check_runtime(self.project)
        self.assertTrue(report["valid"], report)
        self.assertEqual(report["launcher"], "Any Launcher.SH")
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.project.rglob("*") if p.is_file()})

    def test_bundled_transitive_cycle(self):
        self.write("runtime/bin/app", elf(["liba.so"]))
        self.write("runtime/lib/liba.so", elf(["libb.so"], "liba.so"))
        self.write("runtime/lib/libb.so", elf(["liba.so"], "libb.so"))
        report = check_runtime(self.project)
        self.assertTrue(report["valid"], report)
        self.assertEqual(len(report["dependencies"]), 3)

    def test_missing_transitive_dependency(self):
        self.write("runtime/bin/app", elf(["liba.so"]))
        self.write("runtime/lib/liba.so", elf(["libmissing.so"]))
        report = check_runtime(self.project)
        self.assertEqual(self.codes(report), {"MISSING_DEPENDENCY"})
        self.assertEqual(report["errors"][0]["path"], "runtime/lib/liba.so")
        self.assertIn("libmissing.so", report["errors"][0]["action"])

    def test_no_implicit_base_or_host_fallback(self):
        self.write("runtime/bin/app", elf(["libc.so"], interpreter="/lib/ld-musl-mipsel.so.1"))
        report = check_runtime(self.project)
        self.assertEqual(self.codes(report), {"MISSING_DEPENDENCY", "MISSING_INTERPRETER"})
        self.write("lib/ld-musl-mipsel.so.1", elf(), True)
        self.write("lib/libc.so", elf(), True)
        self.assertTrue(check_runtime(self.project, base_root=self.base)["valid"])

    def test_base_transitive_missing_and_wrong_machine(self):
        self.write("runtime/bin/app", elf(["liba.so"]))
        self.write("usr/lib/liba.so", elf(["libb.so"]), True)
        report = check_runtime(self.project, base_root=self.base)
        self.assertIn("MISSING_DEPENDENCY", self.codes(report))
        bad = bytearray(elf())
        struct.pack_into("<H", bad, 18, 62)
        self.write("lib/libb.so", bad, True)
        self.assertIn("ELF_MACHINE", self.codes(check_runtime(self.project, base_root=self.base)))

    def test_endian_float_flags_missing_abi_and_interpreter(self):
        cases = [(elf(order=">"), "ELF_ENDIAN"), (elf(fp=3), "ELF_FLOAT_ABI"),
                 (elf(flags=0x70001407), "ELF_ABI"), (elf(abi=False), "ELF_ABI_UNVERIFIED"),
                 (elf(interpreter="/lib/ld.so.1"), "ELF_INTERPRETER")]
        for data, code in cases:
            with self.subTest(code=code):
                self.write("runtime/bin/app", data)
                self.assertIn(code, self.codes(check_runtime(self.project)))

    def test_elf64_header_identity(self):
        data = bytearray(64)
        data[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHIQQQIHHHHHH", data, 16, 3, 62, 1, 0, 0, 0, 0, 64, 56, 0, 0, 0, 0)
        self.write("runtime/bin/app", data)
        self.assertTrue({"ELF_CLASS", "ELF_MACHINE"} <= self.codes(check_runtime(self.project)))

    def test_malformed_tables_strings_and_truncation(self):
        malformed = [b"\x7fELF", elf(["libx.so"])[:400]]
        bad = bytearray(elf(["libx.so"]))
        struct.pack_into("<I", bad, 388, 999999)
        malformed.append(bad)
        bad = bytearray(elf(["libx.so"]))
        struct.pack_into("<I", bad, 400, 5)
        malformed.append(bad)
        bad = bytearray(elf())
        struct.pack_into("<I", bad, 28, 0xfffffff0)
        malformed.append(bad)
        for data in malformed:
            with self.subTest(size=len(data)):
                self.write("runtime/bin/app", data)
                self.assertIn("ELF_MALFORMED", self.codes(check_runtime(self.project)))

    def test_no_soname_alias_or_nested_search_guess(self):
        self.write("runtime/bin/app", elf(["liba.so"]))
        self.write("runtime/lib/wrong-name.so", elf(soname="liba.so"))
        self.write("runtime/lib/nested/liba.so", elf())
        self.assertIn("MISSING_DEPENDENCY", self.codes(check_runtime(self.project)))

    def test_non_elf_library_rpath_and_needed_path(self):
        self.write("runtime/bin/app", elf(["../liba.so"], runpath="$ORIGIN/../lib"))
        self.write("runtime/lib/bad.so", b"not ELF")
        self.assertTrue({"LIBRARY_NOT_ELF", "ELF_SEARCH_PATH", "UNSUPPORTED_NEEDED_PATH"} <= self.codes(check_runtime(self.project)))

    def test_archive_and_explicit_runtime(self):
        self.write("runtime/bin/app", elf(["liba.so"]))
        extra = self.root / "runtime"
        (extra / "lib").mkdir(parents=True)
        (extra / "lib/liba.so").write_bytes(elf())
        self.assertTrue(check_runtime(self.project, runtime=extra)["valid"])
        output = self.root / "test.lilpck"
        sdk.package(self.project, output, extra)
        self.assertTrue(check_runtime(output)["valid"])

    def test_package_links_and_traversal_refused(self):
        output = self.root / "bad.lilpck"
        sdk.package(self.project, output)
        with zipfile.ZipFile(output, "a") as archive:
            archive.writestr("../escaped", elf())
        with self.assertRaises(sdk.SDKError):
            check_runtime(output)

    def test_target_root_absolute_symlinks_and_cycles(self):
        self.write("lib/libc.so", elf(), True)
        link = self.base / "lib/ld-musl-mipsel.so.1"
        try:
            link.symlink_to("/lib/libc.so")
        except OSError:
            self.skipTest("Host cannot create symlinks")
        self.assertEqual(base_file(self.base, "lib/ld-musl-mipsel.so.1"), self.base / "lib/libc.so")
        link.unlink()
        link.symlink_to("ld-musl-mipsel.so.1")
        with self.assertRaises(ELFError):
            base_file(self.base, "lib/ld-musl-mipsel.so.1")
        link.unlink()
        link.symlink_to("../../outside")
        with self.assertRaises(ELFError):
            base_file(self.base, "lib/ld-musl-mipsel.so.1")

    def test_cli_json_exit_and_no_execution(self):
        self.write("runtime/bin/app", elf(["missing.so"]))
        with mock.patch("subprocess.run", side_effect=AssertionError("must not execute")), contextlib.redirect_stdout(io.StringIO()) as out:
            code = sdk.main(["audit", str(self.project)])
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(out.getvalue())["schema_version"], 1)
        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(sdk.main(["check-runtime", str(self.root / "missing")]), 1)
        self.assertEqual(json.loads(err.getvalue())["errors"][0]["code"], "INVALID_INPUT")

    def test_doctor_missing_tools_and_sdk_has_actionable_json(self):
        with mock.patch("shutil.which", return_value=None), mock.patch("subprocess.run", side_effect=AssertionError("no tools")):
            report = doctor("all", self.root / "missing", gui=True)
        self.assertFalse(report["valid"])
        self.assertTrue({"HOST_CC", "HOST_PKG_CONFIG", "SDK_ROOT"} <= self.codes(report))
        self.assertTrue(all(x["action"] for x in report["errors"]))
        self.assertFalse(report["writes"] or report["downloads"])

    def test_shell_only_is_explicitly_not_qualified(self):
        report = check_runtime(self.project)
        self.assertTrue(report["valid"])
        self.assertEqual(report["warnings"][0]["code"], "NO_ELF")

    def test_native_non_elf_is_an_error(self):
        (self.project / "sdk.json").write_bytes(sdk.json_bytes(dict(format=1, template="sdl2-c")))
        self.write("runtime/bin/application", b"not an executable")
        self.assertIn("NATIVE_NOT_ELF", self.codes(check_runtime(self.project)))

    def test_graph_budgets_are_failures(self):
        self.write("runtime/bin/app", elf(["liba.so", "libb.so"]))
        self.write("runtime/lib/liba.so", elf())
        self.write("usr/lib/libb.so", elf(), True)
        for constant, limit in (("MAX_ELFS", 1), ("MAX_EDGES", 1)):
            with self.subTest(constant=constant), mock.patch.object(runtime_audit, constant, limit):
                self.assertIn("DEPENDENCY_LIMIT", self.codes(check_runtime(self.project, base_root=self.base)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
