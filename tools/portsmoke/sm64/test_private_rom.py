#!/usr/bin/env python3
"""Compare every extracted asset to the user's ROM; retain only test metrics."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import os
import tempfile
import time

spec = importlib.util.spec_from_file_location("manifest", Path(__file__).with_name("generate_manifest.py"))
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


def mio0(data, offset):
    assert data[offset:offset + 4] == b"MIO0"
    size, encoded, raw = struct.unpack_from(">III", data, offset + 4)
    mask, token, literal = offset + 16, offset + encoded, offset + raw
    result = bytearray()
    while len(result) < size:
        flags = data[mask]; mask += 1
        for bit in range(7, -1, -1):
            if len(result) == size:
                break
            if flags & (1 << bit):
                result.append(data[literal]); literal += 1
            else:
                word = struct.unpack_from(">H", data, token)[0]; token += 2
                count, distance = (word >> 12) + 3, (word & 4095) + 1
                assert distance <= len(result) and len(result) + count <= size
                for _ in range(count):
                    result.append(result[-distance])
    return result


def compare(pack_path, rom, assets):
    data = pack_path.read_bytes()
    assert data[:8] == b"PSM64DAT" and struct.unpack_from("<I", data, 8)[0] == 1
    assert hashlib.sha1(data[:-20]).digest() == data[-20:]
    count = struct.unpack_from("<I", data, 12)[0]
    assert count == len(assets)
    offset, blocks, checked = 64, {}, 0
    for expected in assets:
        length, fmt, flags, width, height, start, size = struct.unpack_from("<HBBHHII", data, offset)
        offset += 16
        name = data[offset:offset + length].decode("ascii"); offset += length
        wanted, block, at, size_expected, width_expected, height_expected, fmt_expected = expected
        assert (name, size, width, height, fmt, flags) == (wanted, size_expected, width_expected, height_expected, fmt_expected, 0)
        if block < 0:
            source = rom
        else:
            if block not in blocks:
                blocks[block] = mio0(rom, block)
            source = blocks[block]
        assert data[start:start + size] == source[at:at + size], name
        checked += 1
    assert offset == struct.unpack_from("<I", data, 56)[0]
    return dict(assets_compared=checked, pack_bytes=len(data), compressed_blocks=len(blocks),
                largest_decoded_block=max(map(len, blocks.values())), pack_sha256=hashlib.sha256(data).hexdigest())


def texture_hashes(pack_path, assets):
    """Independent byte-level reference using pinned n64graphics.c scaling."""
    data = pack_path.read_bytes()
    offset, expected, decoded_bytes = 64, {}, 0
    for _ in assets:
        length, fmt, _, width, height, start, size = struct.unpack_from("<HBBHHII", data, offset)
        offset += 16
        name = data[offset:offset + length].decode("ascii"); offset += length
        if fmt not in range(1, 6):
            continue
        raw = data[start:start + size]
        rgba = bytearray()
        for i in range(width * height):
            if fmt == 1:
                word = int.from_bytes(raw[2 * i:2 * i + 2], "big")
                rgba.extend((((word >> 11) & 31) * 255 // 31, ((word >> 6) & 31) * 255 // 31,
                             ((word >> 1) & 31) * 255 // 31, 255 if word & 1 else 0))
                continue
            if fmt == 2:
                intensity = alpha = 255 if raw[i // 8] & (1 << (7 - i % 8)) else 0
            elif fmt == 3:
                nibble = (raw[i // 2] >> (4 if i % 2 == 0 else 0)) & 15
                intensity, alpha = (nibble >> 1) * 36, 255 if nibble & 1 else 0
            elif fmt == 4:
                intensity, alpha = (raw[i] >> 4) * 17, (raw[i] & 15) * 17
            else:
                intensity, alpha = raw[2 * i:2 * i + 2]
            rgba.extend((intensity, intensity, intensity, alpha))
        expected[name] = hashlib.sha1(rgba).hexdigest()
        decoded_bytes += len(rgba)
    return expected, decoded_bytes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--assets-json", type=Path, required=True)
    p.add_argument("--host-binary", type=Path, required=True)
    p.add_argument("--mips-binary", type=Path, required=True)
    p.add_argument("--sysroot", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args()
    source = a.assets_json.read_bytes()
    assert hashlib.sha256(source).hexdigest() == manifest.SOURCE_SHA256
    assets = manifest.convert(json.loads(source))
    rom = a.rom.read_bytes()
    original_hash = hashlib.sha256(rom).hexdigest()
    assert hashlib.sha1(rom).hexdigest() == "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
    a.out.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="portsmoke-sm64-private-") as tmp:
        work = Path(tmp)
        swapped = bytearray(rom)
        for i in range(0, len(rom), 2):
            swapped[i:i + 2] = swapped[i:i + 2][::-1]
        (work / "copy.v64").write_bytes(swapped)
        for i in range(0, len(rom), 4):
            swapped[i:i + 4] = rom[i:i + 4][::-1]
        (work / "copy.n64").write_bytes(swapped)
        variants = [a.rom, work / "copy.v64", work / "copy.n64"]
        for target, command in (("host", [str(a.host_binary)]),
                                ("mips-user-74Kf", ["/usr/bin/qemu-mipsel", "-cpu", "74Kf", "-L", str(a.sysroot), str(a.mips_binary)])):
            expected_pack = None
            bounded = ["prlimit", "--as=33554432:33554432", *command] if target == "host" else command
            for input_path in variants:
                pack = work / (target + input_path.suffix + ".pdat")
                start = time.monotonic()
                env = dict(os.environ, PORTSMOKE_SM64_DIAGNOSTICS="1")
                result = subprocess.run(bounded + [str(input_path), str(pack)], text=True, capture_output=True,
                                        env=env, timeout=120)
                assert result.returncode == 0, result.stdout + result.stderr
                comparison = compare(pack, rom, assets)
                if expected_pack:
                    assert comparison["pack_sha256"] == expected_pack
                expected_pack = comparison["pack_sha256"]
                cache = subprocess.run(command + [str(input_path), str(pack)], text=True, capture_output=True, timeout=120)
                assert cache.returncode == 0 and "Reused verified cache" in cache.stdout
                verify = subprocess.run(command + ["--verify", str(pack)], text=True, capture_output=True, timeout=120)
                assert verify.returncode == 0
                expected, converted_bytes = texture_hashes(pack, assets)
                diagnostics = subprocess.run(bounded + ["--texture-checksums", str(pack)], text=True,
                                             capture_output=True, timeout=120)
                assert diagnostics.returncode == 0, diagnostics.stderr
                actual = dict(line.split("\t")[1:] for line in diagnostics.stdout.splitlines()
                              if line.startswith("TEXTURE\t"))
                assert actual == expected
                results.append(dict(target=target, byte_order=input_path.suffix, status="PASS",
                                    linear_textures_compared=len(expected), converted_rgba_bytes=converted_bytes,
                                    host_address_space_limit_mib=32 if target == "host" else None,
                                    extractor_rss=result.stderr.strip(), rss_is_qemu_process=target != "host",
                                    ru_maxrss_may_include_launcher_high_water=True,
                                    elapsed_seconds=round(time.monotonic() - start, 3), **comparison))
    assert hashlib.sha256(a.rom.read_bytes()).hexdigest() == original_hash
    report = dict(status="PASS", results=results, original_rom_unchanged=True,
                  assets_retained=False, gameplay_tested=False,
                  note="Host/QEMU ru_maxrss can include inherited launcher high-water memory, not just the data tool. Native target RSS is measured in the separate full Linux guest test. User-mode CPU tests do not simulate physical frequency, RAM or peripherals.")
    (a.out / "private-rom-tests.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
