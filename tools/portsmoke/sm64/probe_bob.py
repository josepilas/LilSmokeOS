#!/usr/bin/env python3
"""Private ROM probe: print segment/opcode metrics only, never dump geometry."""
import argparse
from collections import Counter
import hashlib
from pathlib import Path
import struct
from test_private_rom import mio0

ROOTS = [0x07004390, 0x07009D80, 0x0700A470, 0x0700A920, 0x0700DD18, 0x0700E338]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    args = parser.parse_args()
    raw = args.rom.read_bytes()
    assert hashlib.sha1(raw).hexdigest() == "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
    segments = {7: mio0(raw, 4178608), 9: mio0(raw, 3330160)}
    for number, data in segments.items():
        print(f"SEGMENT {number} bytes={len(data)} sha1={hashlib.sha1(data).hexdigest()}")
    ops, textures = Counter(), set()
    triangles = 0
    def walk(address, active):
        nonlocal triangles
        assert address not in active and len(active) < 32
        data = segments[address >> 24]; at = address & 0xffffff
        for _ in range(20000):
            a, b = struct.unpack_from(">II", data, at)
            at += 8; op = a >> 24; ops[op] += 1
            if op == 0xB8:
                return
            if op == 0x06:
                walk(b, [*active, address])
                if a & 0x10000:
                    return
            if op == 0xBF:
                triangles += 1
            if op == 0xFD:
                textures.add(b)
        raise ValueError("unterminated display list")
    for root in ROOTS:
        walk(root, [])
    print("OPCODES", {hex(k): v for k, v in sorted(ops.items())})
    print("TRIANGLES", triangles, "TEXTURE_ADDRESSES", [hex(n) for n in sorted(textures)])
    print("Original ROM read-only; no data retained")


if __name__ == "__main__":
    main()
