#!/usr/bin/env python3
"""Extract only R36SX boot/system dependencies from the exact supplied backup."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import zlib
from pathlib import Path

from prepare_image_plan import ARCHIVE_SHA256, BOOT

UI = {"Arial_en.ttf", "Arial_kr.ttf", "Tahoma.ttf", "font.ttf", "resource", "resource.cpd",
      "UI_Res.cpd", "ui_en.cpd", "ui_pt.cpd", "root.dat", "joystick.cpd", "setting.xml",
      "cores/config.xml", "cores/filelist.xml"}
DENY = {"etc/shadow", "etc/gshadow", "etc/passwd", "etc/group", "etc/machine-id",
        "usr/bin/hcfota", "usr/bin/cubevol-yuan"}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def selection(inventory, probe):
    rows = {member["path"].split("/", 1)[-1]: member
            for member in inventory["members"] if not member["directory"]}
    required = {"cubegm/" + name for name in BOOT} | set(probe["protection_manifest"]["entries"])
    chosen = set(required)
    for path in rows:
        if path.startswith("rootfs/") and path[7:] not in DENY and not path.startswith("rootfs/usr/lib32/"):
            chosen.add(path)
        elif path.startswith("cubegm/"):
            relative = path[7:]
            # The protected frontend can load these before its autorun hook.
            # Preserve menu resources, never game artwork/ROMs or old swap data.
            top_resource = "/" not in relative and (
                re.fullmatch(r"ui_[a-z]{2}\.cpd", relative) or
                relative.endswith(".wav") or relative in ("bgm.mp3", "cubepoweroff.bmp"))
            if relative in UI or top_resource:
                chosen.add(path)
            elif relative.startswith(("lib/", "usr/lib/")) and ".so" in relative:
                # Stock ARM crypto binaries are unrelated to this MIPS boot path.
                if "libcrypto" not in relative:
                    chosen.add(path)
    missing = required - rows.keys()
    if missing:
        raise ValueError("Missing protected/boot paths: " + repr(sorted(missing)))
    busybox = rows["rootfs/bin/busybox"]
    aliases = {path for path in chosen if rows[path]["size"] == busybox["size"]
               and rows[path]["crc32"] == busybox["crc32"] and path != "rootfs/bin/busybox"}
    return rows, sorted(chosen - aliases), sorted(aliases)


def verify_file(path, row):
    data = path.read_bytes()
    if len(data) != row["size"] or f"{zlib.crc32(data):08X}" != (row["crc32"] or "00000000").upper():
        raise ValueError("Archive extraction differs: " + str(path))
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sevenzip", default=shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe")
    parser.add_argument("--resume", action="store_true", help="verify an interrupted owned extraction without decompressing again")
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    probe = json.loads(args.probe.read_text())
    if inventory["sha256"] != ARCHIVE_SHA256 or digest(args.archive) != ARCHIVE_SHA256:
        raise ValueError("Wrong stock archive lineage")
    args.output = args.output.resolve()
    if os.name == "nt" and not str(args.output).startswith("\\\\?\\"):
        args.output = Path("\\\\?\\" + str(args.output))
    if (args.output.exists() and not args.resume) or args.output.is_symlink():
        raise ValueError("Output must be a NEW directory")
    rows, selected, aliases = selection(inventory, probe)
    args.output.mkdir(parents=True, exist_ok=args.resume)
    include = args.output / "selected-members.txt"
    include.write_text("\n".join(rows[path]["path"] for path in selected) + "\n", encoding="utf-8")
    raw = args.output / "extracted"
    if not args.resume:
        subprocess.run([args.sevenzip, "x", "-y", "-spd", "-scsUTF-8", "-bso0", "-bsp0",
                        "-o" + str(raw), "-i@" + str(include), str(args.archive)], check=True)
    prefix = rows["cubegm/dtb.bin"]["path"].split("/", 1)[0]
    stock = raw / prefix
    report = {"archive_sha256": ARCHIVE_SHA256, "files": {}, "busybox_aliases": aliases,
              "roms_extracted": False, "vendor_code_executed": False}
    for path in selected:
        report["files"][path] = verify_file(stock / path, rows[path])
    for alias in aliases:
        target = stock / alias
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(stock / "rootfs/bin/busybox", target)
        report["files"][alias] = verify_file(target, rows[alias])
    # Disable inherited stock passwords; these paths are not integrity-protected.
    accounts = {"passwd": "root:x:0:0:root:/root:/bin/sh\n", "group": "root:x:0:\n",
                "shadow": "root:!:0:0:99999:7:::\n", "gshadow": "root:!::\n"}
    for name, data in accounts.items():
        target = stock / "rootfs/etc" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data.encode("ascii"))
    report["stock_directory"] = str(stock.resolve())
    (args.output / "stock-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"stock_directory": report["stock_directory"], "files": len(report["files"]),
                      "extracted_members": len(selected), "reconstructed_identical_busybox_aliases": len(aliases),
                      "roms_extracted": False}, indent=2))


if __name__ == "__main__":
    main()
