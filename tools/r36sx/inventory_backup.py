#!/usr/bin/env python3
"""Inventory a supplied 7z backup; extract only explicitly selected members."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path, PurePosixPath


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def records(listing):
    _, separator, body = listing.partition("----------\n")
    if not separator:
        raise ValueError("7z technical member separator was not found")
    for block in body.split("\n\n"):
        record = dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
        if "Path" not in record:
            continue
        path = record["Path"].replace("\\", "/")
        if path.startswith("/") or ":" in path or ".." in PurePosixPath(path).parts:
            raise ValueError("Unsafe archive path: " + path)
        yield {"path": path, "size": int(record.get("Size", 0)),
               "directory": "D" in record.get("Attributes", ""),
               "crc32": record.get("CRC", ""), "block": record.get("Block", "")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sevenzip", default=r"C:\Program Files\7-Zip\7z.exe")
    parser.add_argument("--select", action="append", default=[],
                        help="Exact archive-relative member; may be supplied repeatedly")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([args.sevenzip, "l", "-slt", "-sccUTF-8", str(args.archive)],
                         check=True, stdout=subprocess.PIPE)
    listing = run.stdout.decode("utf-8").replace("\r\n", "\n")
    (args.output / "archive-listing.txt").write_text(listing, encoding="utf-8")
    members = list(records(listing))
    files = [member for member in members if not member["directory"]]
    roots = {}
    for member in files:
        parts = member["path"].split("/")
        category = parts[1] if len(parts) > 2 else "<root>"
        group = roots.setdefault(category, {"files": 0, "bytes": 0})
        group["files"] += 1
        group["bytes"] += member["size"]
    report = {"archive": str(args.archive.resolve()), "bytes": args.archive.stat().st_size,
              "sha256": digest(args.archive), "files": len(files),
              "directories": len(members) - len(files),
              "uncompressed_bytes": sum(member["size"] for member in files),
              "categories": roots, "members": members, "selected": []}
    by_path = {member["path"]: member for member in files}
    for selected in args.select:
        if selected not in by_path:
            raise ValueError("Selected file is absent: " + selected)
    if args.select:
        destination = args.output / "selected"
        destination.mkdir(exist_ok=True)
        subprocess.run([args.sevenzip, "x", "-y", "-spd", "-sccUTF-8", "-o" + str(destination),
                        str(args.archive), *args.select], check=True)
        for selected in args.select:
            local = destination / selected
            report["selected"].append({**by_path[selected], "sha256": digest(local)})
    (args.output / "inventory.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary = {key: value for key, value in report.items() if key not in ("members", "selected")}
    summary["boot_candidates"] = [member for member in files
                                  if len(member["path"].split("/")) <= 3
                                  and ("/cubegm/" in member["path"]
                                       or len(member["path"].split("/")) <= 2)]
    summary["selected"] = report["selected"]
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
