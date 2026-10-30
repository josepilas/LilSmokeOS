#!/usr/bin/env python3
"""Generate offset metadata from pinned n64decomp/sm64 assets.json; no ROM needed."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

REVISION = "9921382a68bb0c865e5e45eb594d9c64db59b1af"
SOURCE_SHA256 = "bceace44a08a7851c0f89a2dc39e52f7775e971cabc18feacdae977d95240d92"
FORMATS = {"rgba16": 1, "ia1": 2, "ia4": 3, "ia8": 4, "ia16": 5}
# Upstream's Python slice silently truncates this US skybox to the actual
# MIO0 output (100672 bytes). Make that explicit in the bounded native reader.
LENGTH_FIXUPS = {"textures/skyboxes/bitfs.png": 100672}


def convert(document):
    assets = []
    for name, specification in document.items():
        if not isinstance(specification, list) or "us" not in specification[-1]:
            continue
        positions = specification[-1]["us"]
        if not isinstance(positions[0], int):
            continue  # Samples are covered by the original ctl/tbl pair.
        if name.startswith("@"):
            if name not in ("@sound ctl us", "@sound tbl us"):
                continue
            name = "sound/" + ("sound_data.ctl.be" if "ctl" in name else "sound_data.tbl.be")
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            raise ValueError("unsafe upstream path")
        metadata = specification[:-2]
        size = LENGTH_FIXUPS.get(name, specification[-2])
        block = positions[0] if len(positions) == 2 else -1
        offset = positions[-1]
        width, height = metadata if len(metadata) == 2 else (0, 0)
        fmt = FORMATS.get(path.name.split(".")[-2], 0) if name.endswith(".png") else 0
        if name.startswith("textures/skyboxes/"):
            fmt = 6  # Original skybox RGBA16 tiles + pointer table, not a PNG.
        elif name == "levels/ending/cake.png":
            fmt = 7
        assets.append([name, block, offset, size, width, height, fmt])
    assets.sort()
    if len({row[0] for row in assets}) != len(assets):
        raise ValueError("duplicate assets")
    return assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets_json", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.assets_json.read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("assets.json does not match the pinned upstream commit")
    assets = convert(json.loads(source))
    identity = hashlib.sha1(json.dumps(assets, separators=(",", ":")).encode()).hexdigest()
    lines = ["// Offset metadata only. Source: n64decomp/sm64 (CC0-1.0).",
             f'// Revision: {REVISION}',
             f'// Source SHA-256: {SOURCE_SHA256}',
             f'constexpr const char *manifestIdentity = "{identity}";',
             "constexpr AssetSpec assetSpecs[] = {"]
    for name, block, offset, size, width, height, fmt in assets:
        lines.append(f"    {{{json.dumps(name)}, {block}, {offset}, {size}, {width}, {height}, {fmt}}},")
    lines.append("};\n")
    args.output.write_text("\n".join(lines), encoding="ascii")
    print(f"Generated {len(assets)} metadata entries; manifest {identity}")


if __name__ == "__main__":
    main()
