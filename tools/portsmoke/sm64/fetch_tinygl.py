#!/usr/bin/env python3
"""Fetch only the pinned MIT rasterizer's library sources (under 1 MiB)."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import urllib.request

REVISION = "c2e48591a6bfba1a85f1b87a78dcf1abf7dff57a"
ROOT = Path(__file__).resolve().parents[3]
DEST = ROOT / "package/portsmoke-sm64/src/vendor/tinygl"
LIMIT = 1024 * 1024


def fetch(url, limit):
    request = urllib.request.Request(url, headers={"User-Agent": "LilSmoke-SM64-source-verification"})
    with urllib.request.urlopen(request, timeout=30) as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("source download exceeds budget")
    return data


def main():
    tree = json.loads(fetch(f"https://api.github.com/repos/jserv/tinygl/git/trees/{REVISION}?recursive=1", LIMIT))
    selected = [item for item in tree["tree"] if item["type"] == "blob" and
                (item["path"].startswith(("src/", "include/")) or item["path"] == "LICENSE")]
    if sum(item["size"] for item in selected) > LIMIT or len(selected) > 64:
        raise ValueError("rasterizer source budget exceeded")
    records = []
    for item in selected:
        name = item["path"]
        parts = PurePosixPath(name)
        if parts.is_absolute() or ".." in parts.parts:
            raise ValueError("unsafe upstream path")
        path = DEST / name
        if path.is_symlink():
            raise ValueError("symlink in vendor source")
        data = path.read_bytes() if path.exists() else fetch(
            f"https://raw.githubusercontent.com/jserv/tinygl/{REVISION}/{name}", item["size"])
        blob = b"blob " + str(len(data)).encode() + b"\0" + data
        if len(data) != item["size"] or hashlib.sha1(blob).hexdigest() != item["sha"]:
            raise ValueError(f"upstream/modified-source mismatch: {name}")
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        records.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    lock = dict(repository="https://github.com/jserv/tinygl", revision=REVISION, license="MIT",
                source_bytes=sum(row["bytes"] for row in records), files=records)
    (DEST / "sources.lock.json").write_text(json.dumps(lock, indent=2) + "\n")
    print(f"Verified pinned TinyGL: {len(records)} files, {lock['source_bytes']} bytes; no examples/assets downloaded")


if __name__ == "__main__":
    main()
