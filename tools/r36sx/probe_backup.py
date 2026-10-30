#!/usr/bin/env python3
"""Read-only static R36SX inspection. Does not decode or execute vendor blobs."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fdt(data):
    if len(data) < 40:
        raise ValueError("Truncated FDT header")
    magic, total, structure, strings, _, version, last, _, nstrings, nstructure = struct.unpack_from(">10I", data)
    if magic != 0xD00DFEED or total != len(data) or version < 17 or last > 17:
        raise ValueError("Unsupported FDT header")
    if not 40 <= structure <= total - nstructure or not 40 <= strings <= total - nstrings:
        raise ValueError("FDT blocks outside supplied file")
    names = data[strings:strings + nstrings]
    pos, end = structure, structure + nstructure
    nodes, stack, completed = {}, [], False
    while pos < end:
        if pos + 4 > end:
            raise ValueError("Truncated FDT token")
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        if token == 1:
            zero = data.find(b"\0", pos, end)
            if zero < 0:
                raise ValueError("Unterminated FDT node name")
            stack.append(data[pos:zero].decode("ascii"))
            path = "/" + "/".join(part for part in stack if part)
            if path in nodes:
                raise ValueError("Duplicate FDT node")
            nodes[path] = {}
            pos = (zero + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("Unbalanced FDT end node")
            stack.pop()
        elif token == 3:
            if pos + 8 > end or not stack:
                raise ValueError("Invalid FDT property header")
            size, offset = struct.unpack_from(">II", data, pos)
            pos += 8
            zero = names.find(b"\0", offset)
            if zero < 0 or offset >= len(names) or size > end - pos:
                raise ValueError("FDT property outside supplied blocks")
            name = names[offset:zero].decode("ascii")
            path = "/" + "/".join(part for part in stack if part)
            if name in nodes[path]:
                raise ValueError("Duplicate FDT property")
            nodes[path][name] = data[pos:pos + size]
            pos = (pos + size + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError("Unbalanced FDT tree")
            completed = True
            break
        else:
            raise ValueError("Unknown FDT token")
    if not completed:
        raise ValueError("FDT has no end token")
    return nodes


def cells(value):
    if len(value) % 4:
        raise ValueError("Property is not a cell vector")
    return list(struct.unpack(">" + "I" * (len(value) // 4), value))


def text(value):
    return value.rstrip(b"\0").decode("ascii").split("\0")


def describe(value):
    record = {"bytes": len(value), "hex": value.hex()}
    if not value:
        record["boolean"] = True
    elif value.endswith(b"\0") and any(32 <= c < 127 for c in value) and all(c == 0 or 32 <= c < 127 for c in value):
        record["strings"] = text(value)
    elif not len(value) % 4:
        record["cells"] = cells(value)
    return record


def file_type(data):
    if data.startswith(b"\x7fELF"):
        return "ELF"
    if data.startswith(bytes.fromhex("d00dfeed")):
        return "FDT"
    if data.startswith(bytes.fromhex("27051956")):
        return "uImage"
    if len(data) >= 16 and struct.unpack_from("<I", data, 12)[0] == len(data) - 16:
        return "unknown-16-byte-wrapper-payload-length-matches"
    return "unknown"


def protection_manifest(data, inventory):
    decoded = bytes(value ^ 0x5A for value in data).decode("ascii")
    entries = {}
    by_path = {member["path"].split("/", 1)[-1]: member
               for member in inventory["members"] if not member["directory"]}
    for line in decoded.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"([A-Za-z0-9_./+-]+):\s+0x([A-Fa-f0-9]{8})", line.strip())
        if not match or ".." in match[1].split("/") or match[1].startswith("/"):
            raise ValueError("Unsupported AcXtrnal record")
        path, expected = match[1], int(match[2], 16)
        if path in entries or path not in by_path:
            raise ValueError("Duplicate or missing protected archive member")
        archive_crc = int(by_path[path]["crc32"], 16)
        entries[path] = {"stored_value": f"{expected:08x}",
                         "archive_crc32": f"{archive_crc:08x}",
                         "matches_complemented_crc32": expected == (archive_crc ^ 0xFFFFFFFF)}
    if not entries or not all(row["matches_complemented_crc32"] for row in entries.values()):
        raise ValueError("AcXtrnal complemented-CRC32 check failed")
    return {"encoding_observation": "byte XOR 0x5a decodes printable path/checksum records",
            "checksum_observation": "all listed values equal bitwise NOT of the corresponding 7z member CRC32",
            "entries": entries, "all_archive_crc_matches": True,
            "limitation": "static archive integrity corroboration; not observation of bootloader verification"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("selected", type=Path, help="Extracted selected/usbimager-* root")
    parser.add_argument("output", type=Path)
    parser.add_argument("--m8-profile", type=Path)
    parser.add_argument("--inventory", type=Path)
    args = parser.parse_args()
    result = {"method": "ordinary Python structured FDT/header inspection; not REA MCP evidence",
              "physical_boot_verified": False, "vendor_code_executed": False,
              "source": str(args.selected.resolve()), "files": {}, "trees": {}}
    m8 = json.loads(args.m8_profile.read_text())["files"] if args.m8_profile else {}
    for file in sorted(args.selected.rglob("*")):
        if not file.is_file():
            continue
        data = file.read_bytes()
        name = file.relative_to(args.selected).as_posix()
        row = {"bytes": len(data), "sha256": sha(data), "head16": data[:16].hex(),
               "type": file_type(data)}
        if name.startswith("cubegm/") and name[7:] in m8:
            row["m8_sha256"] = m8[name[7:]]["sha256"]
            row["matches_m8"] = row["sha256"] == row["m8_sha256"]
        result["files"][name] = row
        if row["type"] == "FDT":
            nodes = fdt(data)
            result["trees"][name] = {path: {key: describe(value) for key, value in node.items()}
                                      for path, node in nodes.items()}
    tree = result["trees"]["cubegm/dtb.bin"]
    small = result["trees"]["cubegm/Bubbles.scr"]
    differences = []
    for path in sorted(tree.keys() | small.keys()):
        for key in sorted(tree.get(path, {}).keys() | small.get(path, {}).keys()):
            left, right = tree.get(path, {}).get(key), small.get(path, {}).get(key)
            if left != right:
                differences.append({"node": path, "property": key, "dtb": left, "bubbles": right})
    result["dtb_variant_differences"] = differences
    if args.inventory:
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        result["archive_sha256"] = inventory["sha256"]
        result["protection_manifest"] = protection_manifest(
            (args.selected / "cubegm/AcXtrnal").read_bytes(), inventory)
        result["full_archive_top_level_icube_present"] = any(
            member["path"].split("/", 1)[-1] == "cubegm/icube"
            for member in inventory["members"])
    result["facts"] = {
        "cpu_compatible": tree["/cpus/cpu@0"]["compatible"]["strings"],
        "board": tree["/board"]["label"]["strings"],
        "linux_memory_range_dtb": tree["/memory"]["reg"]["cells"],
        "linux_memory_range_bubbles": small["/memory"]["reg"]["cells"],
        "boot_framebuffer": {key: tree["/hcrtos/boot-fb0"][key]
                             for key in ("xres", "yres", "bits_per_pixel")},
        "rotation": tree["/hcrtos/rotate"]["rotate"]["cells"],
        "panel": tree["/panel"], "audio_i2so": tree["/hcrtos/i2so"],
        "audio_cs4344": tree["/hcrtos/cs4344"],
        "external_files": tree["/hcrtos/external_files"],
        "has_top_level_icube": (args.selected / "cubegm/icube").is_file(),
        "icube_absence_scope": "selected extraction only; cross-check full archive inventory",
        "bootargs": tree["/chosen"]["bootargs"]["strings"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"facts": result["facts"], "variant_differences": differences,
                      "files": result["files"]}, indent=2))


if __name__ == "__main__":
    main()
