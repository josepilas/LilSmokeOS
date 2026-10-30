#!/usr/bin/env python3
"""Real SquashFS hostile fixtures; test path overrides exist only in QA builds."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import unittest

HELPER = []
LIBPATH = ""

def elf(interpreter=None, dependency=None):
    data = bytearray(512)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", data, 16, 2, 8, 1)
    struct.pack_into("<I", data, 28, 52)
    struct.pack_into("<IHHH", data, 36, 0x70001000, 52, 32, 3 if interpreter else 1)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0, 0x400000, 0, 512, 512, 5, 4096)
    if interpreter:
        name = interpreter.encode() + b"\0"
        data[256:256 + len(name)] = name
        struct.pack_into("<IIIIIIII", data, 84, 3, 256, 0, 0, len(name), len(name), 4, 1)
        struct.pack_into("<IIIIIIII", data, 116, 2, 320, 0, 0, 32, 32, 4, 4)
        struct.pack_into("<IIIIIIII", data, 320, 5, 0x400180, 10, 64, 1, 0, 0, 0)
        lib = (dependency or "libc.so").encode() + b"\0"
        data[384:384 + len(lib)] = lib
    return bytes(data)

def desktop(exec_line="app %f", extra="", name="Fixture"):
    return ("[Desktop Entry]\nType=Application\nName=" + name + "\nExec=" +
            exec_line + "\nX-LilSmoke-Systems=snes\n" + extra).encode()

class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="od-fixture-")
        self.root = Path(self.tmp.name)
        self.apps = self.root / "apps"
        self.apps.mkdir()
        self.cache = self.root / "cache"
        self.home = self.root / "home"
        self.env = dict(os.environ, OD_TEST_ROOT0=str(self.apps),
                        OD_TEST_ROOT1=str(self.root / "absent"),
                        OD_TEST_CACHE=str(self.cache), OD_TEST_HOME=str(self.home),
                        LD_LIBRARY_PATH=LIBPATH)
    def tearDown(self):
        self.tmp.cleanup()
    def call(self, *args):
        return subprocess.run(HELPER + list(args), env=self.env, text=True,
                              capture_output=True, timeout=8 if args[0] == "scan" else 40)
    def image(self, files=None, compression="gzip", block=131072, extra=()):
        tree = self.root / ("tree-" + str(len(list(self.root.glob("tree-*")))))
        tree.mkdir()
        for name, content in (files or {"test.desktop": desktop(), "app": elf()}).items():
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            if name == "app":
                path.chmod(0o755)
        for name, target in extra:
            (tree / name).symlink_to(target)
        out = self.apps / (tree.name + ".opk")
        subprocess.run(["mksquashfs", str(tree), str(out), "-noappend", "-processors", "1",
                        "-no-progress", "-comp", compression, "-b", str(block),
                        "-noI", "-noD", "-noF"], check=True, stdout=subprocess.DEVNULL)
        return out, tree
    def rows(self):
        p = self.call("scan")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertLess(len(p.stdout.encode()), 128 * 1024)
        rows = [line.split("\t") for line in p.stdout.splitlines()]
        for row in rows:
            self.assertEqual(len(row), 5)
            self.assertRegex(row[0], r"^od-[0-9a-f]{64}$")
            self.assertIn(row[4], ("0", "1"))
        return rows
    def test_gzip_metadata_no_extract(self):
        self.image()
        row, = self.rows()
        self.assertEqual(row[1:], ["Fixture", "snes", "", "1"])
        self.assertFalse(self.cache.exists())
    def test_xz_streaming(self):
        self.image({"test.desktop": desktop(), "app": elf(), "data/big": b"abcd" * 70000}, "xz")
        row, = self.rows()
        self.assertEqual(row[3], "")
        rom = self.root / "space ;literal.sfc"
        rom.write_bytes(b"rom")
        p = self.call("run", row[0], str(rom))
        # Foreign-architecture synthetic ELF cannot execute on the host.
        self.assertEqual(p.returncode, 3, p.stdout + p.stderr)
        payload, = list(self.cache.glob("*/payload"))
        self.assertEqual((payload / "data/big").read_bytes(), b"abcd" * 70000)
        self.assertEqual((payload / "app").read_bytes(), elf())
        self.assertTrue((payload.parent / "complete").exists())
    def test_tampered_cache_rejected(self):
        self.image()
        row, = self.rows()
        rom = self.root / "rom.sfc"
        rom.write_bytes(b"rom")
        self.call("run", row[0], str(rom))
        payload, = list(self.cache.glob("*/payload"))
        (payload / "app").write_bytes(b"bad")
        p = self.call("run", row[0], str(rom))
        self.assertIn("Cache content changed", p.stderr)
    def test_cache_symlink_rejected(self):
        image, _ = self.image()
        row, = self.rows()
        self.cache.mkdir()
        (self.cache / hashlib.sha256(image.read_bytes()).hexdigest()).symlink_to(self.root, target_is_directory=True)
        rom = self.root / "rom.sfc"
        rom.write_bytes(b"rom")
        p = self.call("run", row[0], str(rom))
        self.assertIn("unsafe", p.stderr.lower())
    def test_cache_budget(self):
        self.image()
        row, = self.rows()
        self.cache.mkdir()
        with (self.cache / "full").open("wb") as f:
            f.truncate(512 * 1024 * 1024)
        rom = self.root / "rom.sfc"
        rom.write_bytes(b"rom")
        p = self.call("run", row[0], str(rom))
        self.assertIn("budget", p.stderr)
    def test_legacy_and_gpu(self):
        for interpreter, dep in [("/lib/ld-uClibc.so.0", None),
                                 ("/lib/ld.so.1", "libc.so.6"),
                                 ("/lib/ld-musl-mipsel.so.1", "libGLESv2.so.2")]:
            image, _ = self.image({"test.desktop": desktop(), "app": elf(interpreter, dep)})
            row, = self.rows()
            self.assertTrue(row[3], row)
            image.unlink()
    def test_musl_dynamic_guard(self):
        self.image({"test.desktop": desktop(), "app": elf("/lib/ld-musl-mipsel.so.1")})
        row, = self.rows()
        self.assertEqual(row[3], "")
    def test_links_rejected(self):
        self.image(extra=[("escape", "/etc/passwd")])
        row, = self.rows()
        self.assertIn("links", row[3])
    def test_fat_case_collision(self):
        self.image({"test.desktop": desktop(), "app": elf(), "A": b"a", "a": b"b"})
        row, = self.rows()
        self.assertIn("collision", row[3])
    def test_depth_bound(self):
        self.image({"test.desktop": desktop(), "app": elf(), "/".join(["d"] * 18) + "/x": b"x"})
        row, = self.rows()
        self.assertIn("depth", row[3])
    def test_block_bound(self):
        self.image(block=1048576)
        row, = self.rows()
        self.assertIn("bounds", row[3])
    def test_traversal_filename(self):
        image, _ = self.image({"evil.desktop": desktop(), "app": elf()})
        raw = image.read_bytes()
        self.assertIn(b"evil.desktop", raw)
        image.write_bytes(raw.replace(b"evil.desktop", b"../x.desktop"))
        row, = self.rows()
        self.assertTrue(row[3])
    def test_corruption(self):
        path = self.apps / "bad.opk"
        for raw in (b"bad", b"hsqs" + b"\xff" * 200):
            path.write_bytes(raw)
            row, = self.rows()
            self.assertTrue(row[3])
    def test_native_relative_and_rejected_exec(self):
        (self.apps / "app").write_bytes(elf())
        (self.apps / "app").chmod(0o755)
        p = self.apps / "native.desktop"
        p.write_bytes(desktop())
        row, = self.rows()
        self.assertEqual(row[3], "")
        for value in ("../escape %f", "/bin/sh -c touch", "app ; touch x", "app prefix%f", "app %x"):
            p.write_bytes(desktop(value))
            row, = self.rows()
            self.assertTrue(row[3], value)
    def test_malformed_metadata(self):
        p = self.apps / "bad.desktop"
        for raw in (b"[Desktop Entry]\n \t", desktop(extra="Exec=app\n"),
                    desktop(extra="Type=Link\n"), desktop(name="bad\tname"), b"\0bad"):
            p.write_bytes(raw)
            row, = self.rows()
            self.assertTrue(row[3])
    def test_catalog_bound(self):
        for n in range(150):
            (self.apps / ("%03d.desktop" % n)).write_bytes(desktop("/bin/sh"))
        started = time.monotonic()
        self.assertEqual(len(self.rows()), 128)
        self.assertLess(time.monotonic() - started, 8)
    def test_rom_required_and_unknown(self):
        self.image()
        row, = self.rows()
        self.assertIn("requires a ROM", self.call("run", row[0]).stderr)
        self.assertIn("Unknown", self.call("run", "../bad").stderr)
    def test_mkdir_lock_busy_and_stale(self):
        self.image()
        row, = self.rows()
        lock = self.cache / ".lock"
        lock.mkdir(parents=True)
        owner = lock / "owner"
        owner.write_text(str(os.getpid()))
        rom = self.root / "rom.sfc"
        rom.write_bytes(b"rom")
        self.assertIn("busy", self.call("run", row[0], str(rom)).stderr)
        self.assertEqual(owner.read_text(), str(os.getpid()))
        owner.write_text("2147483647")
        self.assertIn("Stale", self.call("run", row[0], str(rom)).stderr)
        self.assertTrue(lock.exists())
    def test_extra_cache_content(self):
        self.image()
        row, = self.rows()
        rom = self.root / "rom.sfc"
        rom.write_bytes(b"rom")
        self.call("run", row[0], str(rom))
        self.assertFalse((self.cache / ".lock").exists())
        payload, = list(self.cache.glob("*/payload"))
        (payload / "extra").write_bytes(b"unexpected")
        self.assertIn("Unexpected", self.call("run", row[0], str(rom)).stderr)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", required=True)
    parser.add_argument("--library-path", required=True)
    parser.add_argument("--qemu", default="")
    parser.add_argument("--sysroot", default="")
    args = parser.parse_args()
    HELPER = ([args.qemu, "-cpu", "74Kf", "-L", args.sysroot] if args.qemu else []) + [args.helper]
    LIBPATH = args.library_path
    unittest.main(argv=["test_runtime"], verbosity=2)
