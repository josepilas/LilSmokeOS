#!/usr/bin/env python3
"""Snapshot selected primary-source text at immutable Git revisions for review."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen

SOURCES = {
    "tzubertowski/TreeFrogUI": ["README.md", "install.md", "build_release.sh",
                               "hijack/zhijack.tpl.sh", "hijack/tfhijack.c",
                               "hijack/build_tfhijack.sh", "LICENSE.md"],
    "Witali/r36sx_disasm": ["README.md", "BOOT_ROUTE_FINDINGS.md", "FIRMWARE_FINDINGS.md",
                             "build-own-linux-feature/Bubbles_vs_dtb.md",
                             "tools/repack_debian12_bootonly_image_wsl.sh"],
    "700zx1/mini_rkgame": ["mini_rkgame.c", "driver_so.h", "LICENSE"],
    "700zx1/sf3000-dev": ["Dockerfile"],
    "SiarheiVolkau/sf3k-hacking": ["Readme.md"],
}


def fetch(url):
    with urlopen(Request(url, headers={"User-Agent": "LilSmokeOS-offline-firmware-research"}), timeout=30) as response:
        body = response.read(1024 * 1024 + 1)
    if len(body) > 1024 * 1024:
        raise ValueError("Text source exceeds budget")
    return body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"method": "read-only HTTPS primary-source snapshot; no downloaded code executed", "sources": []}
    for repo, paths in SOURCES.items():
        revision = json.loads(fetch("https://api.github.com/repos/" + repo + "/commits/main"))["sha"]
        for path in paths:
            url = "https://raw.githubusercontent.com/" + repo + "/" + revision + "/" + path
            body = fetch(url)
            target = args.output / repo / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            manifest["sources"].append({"repository": repo, "revision": revision,
                                        "path": path, "url": url, "bytes": len(body),
                                        "sha256": hashlib.sha256(body).hexdigest()})
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
