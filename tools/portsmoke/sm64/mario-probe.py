#!/usr/bin/env python3
"""Read-only private ROM probe; prints metadata/hashes, never vertex/animation bytes."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
from test_private_rom import mio0

REVISION = "9921382a68bb0c865e5e45eb594d9c64db59b1af"
SOURCE_SHA256 = {
    "LICENSE.md": "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499",
    "actors/mario/geo.inc.c": "5c1190829eb3ddaeef3b7209afd47c1182d2db539ec5b977755380001b0db0e0",
    "actors/mario/model.inc.c": "226ed415bb0d6eaffbde76a5274e7983f2df5d44e03db0987e9a8ff818c718cc",
    "tools/mario_anims_converter.py": "634f4ac63773d8b7718644405152fa132bbafcf64f94c2d1e7aa59f3c3a0ca71",
    "src/game/rendering_graph_node.c": "09ab44b3b92be519b54f0c971d78a975ade627de600548e822ab337db4e2dcfb",
    "src/engine/math_util.c": "9cca7513caf9831a9525ff2ea6869b1fbeab2d09731a0b18f46b81d08c6dc119",
}

NAMES = ["mario_butt", "mario_torso", "mario_cap_on_eyes_front", "mario_left_arm",
         "mario_left_forearm_shared_dl", "mario_left_hand_closed", "mario_right_arm",
         "mario_right_forearm_shared_dl", "mario_right_hand_closed", "mario_left_thigh",
         "mario_left_leg_shared_dl", "mario_left_foot", "mario_right_thigh",
         "mario_right_leg_shared_dl", "mario_right_foot", "mario_right_foot_dl"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--upstream", type=Path, default=Path("/root/lilsmoke-sm64-base"))
    args = parser.parse_args()
    for name, expected in SOURCE_SHA256.items():
        assert hashlib.sha256((args.upstream / name).read_bytes()).hexdigest() == expected, name
    rom = args.rom.read_bytes()
    assert hashlib.sha1(rom).hexdigest() == "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
    segment = mio0(rom, 1132368)
    model = (args.upstream / "actors/mario/model.inc.c").read_text()
    symbols, last = {}, None
    for line in model.splitlines():
        match = re.match(r"// (0x04[0-9A-Fa-f]{6})", line)
        if match: last = int(match[1], 16)
        match = re.match(r"(?:static )?const Gfx (\w+)\[\] =", line)
        if match: symbols[match[1]] = last
    metadata = {}
    for name in NAMES:
        counts = Counter(); textures = set(); lights = set(); triangles = [0]
        def walk(address, active):
            assert address >> 24 == 4 and address not in active and len(active) < 32
            at = address & 0xffffff
            for _ in range(10000):
                a, b = struct.unpack_from(">II", segment, at); at += 8
                op = a >> 24; counts[hex(op)] += 1
                if op == 0xB8: return
                if op == 0x06:
                    walk(b, [*active, address])
                    if a & 0x10000: return
                if op == 0xBF: triangles[0] += 1
                if op == 0xFD: textures.add(hex(b))
                if op == 0x03: lights.add(hex(b))
            raise ValueError("unterminated Mario display list")
        walk(symbols[name], [])
        metadata[name] = dict(address=hex(symbols[name]), triangles=triangles[0],
                              opcodes=dict(counts), textures=sorted(textures), lights=sorted(lights))
    needle = struct.pack(">II", 209, 0)
    candidates = [m.start() for m in re.finditer(re.escape(needle), rom)]
    tables = []
    for base in candidates:
        clips = {}
        for anim in [0x48, 0x4D, 0xC5]:
            offset, size = struct.unpack_from(">II", rom, base + 8 + anim * 8)
            if not (24 <= size < 65536 and 0 < offset < 1024 * 1024 and base + offset + size <= len(rom)):
                break
            raw = rom[base + offset:base + offset + size]
            header = struct.unpack_from(">6hIII", raw)
            clips[hex(anim)] = dict(rom_offset=base + offset, bytes=size, header=header,
                                   sha1=hashlib.sha1(raw).hexdigest())
        if len(clips) == 3: tables.append(dict(base=hex(base), clips=clips))
    report = dict(upstream_revision=REVISION, upstream_metadata_license="CC0-1.0",
                  assets_distributable=False, segment4_rom=1132368, segment4_bytes=len(segment),
                  segment4_sha1=hashlib.sha1(segment).hexdigest(), parts=metadata, animation_tables=tables,
                  metadata_hashes=SOURCE_SHA256)
    output = json.dumps(report, indent=2) + "\n"
    build = Path(__file__).resolve().parent / "_build/mario-worker"
    build.mkdir(parents=True, exist_ok=True)
    (build / "mario-upstream-results.json").write_text(output)
    print(output)


if __name__ == "__main__":
    main()
