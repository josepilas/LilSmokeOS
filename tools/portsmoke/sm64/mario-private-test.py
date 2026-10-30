#!/usr/bin/env python3
"""Compare all four private actor payloads; keep reports only, never game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from test_private_rom import mio0

ASSETS = [(-1, 5715020, 2218), (-1, 5408288, 2756), (1132368, 0, 217976), (-1, 5397832, 5374)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    parser.add_argument("--build", type=Path, default=Path(__file__).parent / "_build/mario-worker")
    args = parser.parse_args()
    rom = args.rom.read_bytes(); original_hash = hashlib.sha256(rom).hexdigest()
    assert hashlib.sha1(rom).hexdigest() == "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
    reports = []
    with tempfile.TemporaryDirectory(prefix="sm64-mario-private-") as tmp:
        root = Path(tmp)
        variants = [args.rom]
        for stride in (2, 4):
            swapped = bytearray(rom)
            for i in range(0, len(swapped), stride): swapped[i:i+stride] = swapped[i:i+stride][::-1]
            path = root / f"order-{stride}.private"; path.write_bytes(swapped); variants.append(path)
        for target in ("host", "mips", "asan"):
            prefix = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(args.sdk / "staging")] if target == "mips" else []
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1")
            def call(binary, *words, good=True):
                result = subprocess.run([*prefix, str(args.build / f"{target}-{binary}"), *map(str, words)],
                                        env=env, text=True, capture_output=True, timeout=120)
                assert (result.returncode == 0) == good, (target, result.stdout, result.stderr)
                assert "runtime error:" not in result.stderr and "AddressSanitizer" not in result.stderr, result.stderr
                return result.stdout
            for number, source in enumerate(variants):
                pack = root / f"{target}-{number}.pdat"
                output = call("private", source, pack)
                assert "MarioParts=15 MarioTriangles=752" in output
                data = pack.read_bytes(); at = 64
                assert data[:8] == b"PSM64DAT" and struct.unpack_from("<I", data, 12)[0] == 4
                for block, offset, length in ASSETS:
                    name_len, fmt, flags, w, h, start, size = struct.unpack_from("<HBBHHII", data, at)
                    at += 16 + name_len
                    assert (fmt, flags, w, h, size) == (0, 0, 0, 0, length)
                    source_bytes = rom if block < 0 else mio0(rom, block)
                    assert data[start:start+size] == source_bytes[offset:offset+length]
                checks = call("test", pack)
                before = pack.read_bytes(); call("private", source, pack); assert before == pack.read_bytes()
                reports.append(dict(target=target, order=number, status="PASS", pack_bytes=len(data),
                                    pack_sha256=hashlib.sha256(data).hexdigest(), output=output.strip().splitlines(),
                                    checks=checks.strip().splitlines()))
            # Corrupt payload/index/footer must not become valid cached geometry.
            pack = root / f"{target}-0.pdat"; data = bytearray(pack.read_bytes()); data[-1] ^= 1; pack.write_bytes(data)
            call("test", pack, good=False)
            call("private", args.rom, pack)
            assert "MarioTriangles=752" in call("private", args.rom, pack)
            # Even a recomputed whole-pack checksum cannot admit modified game payloads.
            data = bytearray(pack.read_bytes())
            start = struct.unpack_from("<I", data, 72)[0]
            data[start + 32] ^= 1
            data[-20:] = hashlib.sha1(data[:-20]).digest()
            pack.write_bytes(data)
            call("test", pack, good=False)
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == original_hash
    result = dict(status="PASS", private_data_retained=False, original_rom_unchanged=True,
                  original_animations=["C5 idle head center", "48 walking", "4D single jump"], results=reports)
    args.build.mkdir(parents=True, exist_ok=True)
    (args.build / "mario-private-results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
