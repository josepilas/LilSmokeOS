#!/usr/bin/env python3
"""Pack the project's MIT-licensed regression fixture, not a game download."""
from pathlib import Path
import struct
import sys

code = Path(sys.argv[1]).read_bytes()
assert 0 < len(code) <= 0xFC0
rom = bytearray(0x2000)
struct.pack_into(">4I", rom, 0, 0x80371240, 0xF, 0xA4000040, 0)
rom[0x20:0x34] = b"LILSMOKE RDP SI TEST"
rom[0x3E] = ord("E")
rom[0x40:0x40+len(code)] = code
path = Path(sys.argv[2])
assert len(rom) == 0x2000
path.write_bytes(rom)
for suffix, mask in [(".v64", 1), (".n64", 3)]:
    variant = bytearray(len(rom))
    for index, value in enumerate(rom):
        variant[index ^ mask] = value
    path.with_suffix(suffix).write_bytes(variant)
