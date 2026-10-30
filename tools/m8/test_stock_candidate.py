#!/usr/bin/env python3
import gzip
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from stock_format import inspect, kernel_scripts, mbr, uimage, xor, BOOT_BYTES
from build_stock_candidate import prepare_runtime, valid_path


def header(payload, arch=5, kind=2, compression=1):
    raw = bytearray(struct.pack(">7I4B32s", 0x27051956, 0, 0, len(payload), 0x80000000,
                               0x80254b20, zlib.crc32(payload), 5, arch, kind, compression,
                               b"vmlinux"))
    struct.pack_into(">I", raw, 4, zlib.crc32(raw))
    return bytes(raw) + payload


def cpio(entries):
    data = bytearray()
    for name, content in list(entries.items()) + [("TRAILER!!!", b"")]:
        label = name.encode() + b"\0"
        fields = [1, 0o100644, 0, 0, 1, 0, len(content), 0, 0, 0, 0, len(label), 0]
        data += b"070701" + b"".join(f"{value:08x}".encode() for value in fields) + label
        data += bytes((-len(data)) % 4)
        data += content + bytes((-len(content)) % 4)
    return data


class StockTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def image(self, encoded=True):
        partition = 2048
        mbr_data = bytearray(512)
        mbr_data[510:] = b"\x55\xaa"
        mbr_data[446:462] = struct.pack("<B3sB3sII", 128, bytes(3), 12, bytes(3), partition, 8192)
        path = self.root / "stock.img"
        with path.open("wb") as stream:
            prefix = bytes(mbr_data) + bytes(BOOT_BYTES - 512)
            stream.write(xor(prefix) if encoded else prefix)
            boot = bytearray(512)
            struct.pack_into("<H", boot, 11, 512)
            boot[13] = 8
            struct.pack_into("<I", boot, 32, 8192)
            boot[71:82] = b"LILOS      "
            boot[82:90] = b"FAT32   "
            boot[510:] = b"\x55\xaa"
            stream.write(boot)
            stream.truncate((partition + 8192) * 512)
        return path

    def test_xor_roundtrip(self):
        data = bytes(range(256)) * 3
        self.assertEqual(xor(xor(data)), data)

    def test_xor_offset(self):
        data = bytes(range(256))
        self.assertEqual(xor(data)[17:95], xor(data[17:95], 17))

    def test_encoded_image_read_only(self):
        path = self.image()
        before = path.read_bytes()
        result = inspect(path)
        self.assertEqual(result["encoding"], "hc16xx-xor-deadbeef")
        self.assertEqual(result["partitions"][0]["label"], "LILOS")
        self.assertEqual(path.read_bytes(), before)

    def test_plain_reference(self):
        self.assertEqual(inspect(self.image(False))["encoding"], "plain")

    def test_truncated_backup(self):
        path = self.root / "short.img"; path.write_bytes(b"short")
        with self.assertRaises(ValueError): inspect(path)

    def test_unknown_encoding(self):
        path = self.image()
        with path.open("r+b") as stream: stream.seek(510); stream.write(b"xx")
        with self.assertRaises(ValueError): inspect(path)

    def test_symlink_input(self):
        path = self.image(); link = self.root / "link.img"; link.symlink_to(path)
        with self.assertRaises(ValueError): inspect(link)

    def test_out_of_range_partition(self):
        data = bytearray(512); data[510:] = b"\x55\xaa"
        data[446:462] = struct.pack("<B3sB3sII", 128, bytes(3), 12, bytes(3), 2048, 9999999)
        with self.assertRaises(ValueError): mbr(data, 512 * 10000)

    def test_overlap(self):
        data = bytearray(512); data[510:] = b"\x55\xaa"
        for i, start in enumerate((2048, 4096)):
            data[446 + i * 16:462 + i * 16] = struct.pack("<B3sB3sII", 0, bytes(3), 12, bytes(3), start, 4000)
        with self.assertRaises(ValueError): mbr(data, 512 * 10000)

    def test_uimage_crc(self):
        path = self.root / "kernel.uImage"; path.write_bytes(header(gzip.compress(b"kernel")))
        self.assertEqual(uimage(path)["architecture"], 5)
        self.assertEqual(uimage(path)["entry"], 0x80254b20)

    def test_uimage_payload_corruption(self):
        data = bytearray(header(gzip.compress(b"kernel"))); data[-1] ^= 1
        path = self.root / "bad.uImage"; path.write_bytes(data)
        with self.assertRaises(ValueError): uimage(path)

    def test_uimage_header_corruption(self):
        data = bytearray(header(gzip.compress(b"kernel"))); data[12] ^= 1
        path = self.root / "bad.uImage"; path.write_bytes(data)
        with self.assertRaises(ValueError): uimage(path)

    def test_wrong_architecture(self):
        path = self.root / "arm.uImage"; path.write_bytes(header(gzip.compress(b"kernel"), arch=2))
        with self.assertRaises(ValueError): uimage(path)

    def test_stock_startup_extraction(self):
        payload = b"Linux version test\0" + bytes(12) + cpio({"etc/init.d/S99app": b"/mnt/sdcard/cubegm/icube &\n"})
        path = self.root / "kernel.uImage"; path.write_bytes(header(gzip.compress(payload)))
        self.assertEqual(kernel_scripts(path)["scripts"]["etc/init.d/S99app"], "/mnt/sdcard/cubegm/icube &\n")

    def test_runtime_file_backing(self):
        target = self.root / "target"; (target / "bin").mkdir(parents=True)
        (target / "etc").mkdir(); (target / "bin/tool").write_bytes(b"ELF fixture")
        (target / "etc/shadow").write_text("private")
        (target / "bin/sh").symlink_to("tool")
        destination = self.root / "payload"; destination.mkdir()
        result = prepare_runtime(target, destination)
        text = (destination / "runtime.tsv").read_text()
        self.assertIn("F\t", text); self.assertIn("L\t", text); self.assertIn("C\t", text)
        self.assertEqual(result["objects"], 2)
        self.assertEqual(sorted(p.read_bytes() for p in (destination / "files").iterdir()), [b"ELF fixture", b"private"])

    def test_omit_foreign_kernel_modules(self):
        target = self.root / "target"; (target / "lib/modules/5.15").mkdir(parents=True)
        (target / "lib/modules/5.15/module.ko").write_bytes(b"not the device kernel")
        destination = self.root / "payload"; destination.mkdir()
        self.assertEqual(prepare_runtime(target, destination)["objects"], 0)

    def test_manifest_path_validation(self):
        for path in (Path("/absolute"), Path("../escape"), Path("bad\tfield"), Path("bad\\field")):
            with self.subTest(path=path), self.assertRaises(ValueError): valid_path(path)

    def test_special_runtime_file(self):
        import os
        target = self.root / "target"; target.mkdir(); os.mkfifo(target / "fifo")
        destination = self.root / "payload"; destination.mkdir()
        with self.assertRaises(ValueError): prepare_runtime(target, destination)


if __name__ == "__main__": unittest.main()
