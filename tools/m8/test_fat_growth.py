#!/usr/bin/env python3
"""File-only integration tests for the HC16xx no-loop FAT growth path."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from build_stock_candidate import BOARD, assemble, prepare_growable_fat, run
from stock_format import inspect


@unittest.skipUnless(all(shutil.which(v) for v in ("gcc", "mkfs.vfat", "mcopy", "mmd", "mtype", "fsck.vfat")),
                     "host compiler and FAT tools required")
class GrowthTests(unittest.TestCase):
    def test_grow_retry_refusal_and_rom_preservation(self):
        with tempfile.TemporaryDirectory(prefix="m8-growth-test-", dir="/root") as temporary:
            work = Path(temporary)
            binary = work / "grow"
            run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", BOARD / "m8-grow.c", "-o", binary])
            p1, p2 = work / "os.fat", work / "roms.fat"
            with p1.open("wb") as stream: stream.truncate(64 * 1024 * 1024)
            run(["mkfs.vfat", "-F", "32", "-n", "LILOS", p1])
            start = 2048 + 64 * 2048
            prepare_growable_fat(p2, 1024, start, 131072)
            sample = work / "sample.bin"
            sample.write_bytes(bytes(range(256)) * 1024)
            run(["mmd", "-i", p2, "::/roms", "::/roms/snes"])
            run(["mcopy", "-i", p2, sample, "::/roms/snes/space name.sfc"])
            seed_check = run(["fsck.vfat", "-n", p2])
            self.assertIn("files", seed_check)
            prefix = bytearray(1024 * 1024)
            prefix[510:512] = b"\x55\xaa"
            for slot, (offset, sectors, active) in enumerate(((2048, 64 * 2048, 128), (start, 1024 * 2048, 0))):
                prefix[446 + slot * 16:462 + slot * 16] = struct.pack("<B3sB3sII", active, b"\xfe\xff\xff", 0xc, b"\xfe\xff\xff", offset, sectors)
            image = work / "card.img"
            assemble(image, prefix, p1, p2)
            with image.open("r+b") as stream: stream.truncate(4 * 1024 * 1024 * 1024)
            no_flock = work / "grow-no-flock"
            run(["gcc", "-DM8_TEST_NO_FLOCK", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", BOARD / "m8-grow.c", "-o", no_flock])
            lock = work / "boot-lock"; lock.mkdir(mode=0o700)
            owner = lock / "owner"
            owner.write_text(str(os.getpid() + 1) + "\n"); owner.chmod(0o600)
            environment = dict(os.environ, M8_TEST_BOOT_LOCK=str(lock))
            refused = subprocess.run([str(no_flock), "--test-image", str(image)], env=environment, capture_output=True)
            self.assertEqual(refused.returncode, 2)
            owner.write_text(str(os.getpid()) + "\n")
            with image.open("rb") as stream:
                stream.seek(1048576); os_before = stream.read(1024 * 1024)
            simulated = subprocess.run([str(no_flock), "--test-image", str(image)], env=environment,
                                       capture_output=True, text=True)
            self.assertEqual(simulated.returncode, 0, simulated.stderr)
            self.assertIn("parent-owned atomic boot lock", simulated.stdout)
            output = run([binary, "--test-image", image])
            self.assertIn("verified", output)
            parsed = inspect(image)
            self.assertEqual(parsed["partitions"][1]["sectors"], 4 * 1024 * 2048 - start)
            exported = work / "exported.bin"
            run(["mcopy", "-i", f"{image}@@{start * 512}", "::/roms/snes/space name.sfc", exported])
            self.assertEqual(hashlib.sha256(exported.read_bytes()).digest(), hashlib.sha256(sample.read_bytes()).digest())
            with image.open("rb") as stream:
                stream.seek(1048576); self.assertEqual(stream.read(len(os_before)), os_before)
                stream.seek(start * 512); grown_boot = stream.read(512)
            run([binary, "--test-image", image])
            with image.open("rb") as stream:
                stream.seek(start * 512); self.assertEqual(stream.read(512), grown_boot)
            # Simulate a loss of power after the MBR and backup BPB were committed.
            with image.open("r+b") as stream:
                stream.seek(start * 512)
                seed_boot = bytearray(stream.read(512))
                struct.pack_into("<I", seed_boot, 32, 1024 * 2048)
                stream.seek(start * 512); stream.write(seed_boot)
            run([binary, "--test-image", image])
            with image.open("rb") as stream:
                stream.seek(start * 512); self.assertEqual(stream.read(512), grown_boot)
                stream.seek(0); mbr_before = stream.read(512)
            # A card beyond the reserved FAT capacity is refused before writes.
            with image.open("r+b") as stream: stream.truncate(256 * 1024 * 1024 * 1024)
            refused = subprocess.run([str(binary), "--test-image", str(image)], capture_output=True)
            self.assertNotEqual(refused.returncode, 0)
            with image.open("rb") as stream:
                self.assertEqual(stream.read(512), mbr_before)
                stream.seek(start * 512); self.assertEqual(stream.read(512), grown_boot)


if __name__ == "__main__": unittest.main()
