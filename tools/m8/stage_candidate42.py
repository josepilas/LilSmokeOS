#!/usr/bin/env python3
"""Overlay qualified UI-only changes on the physically qualified C41 runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--baseline", required=True, type=Path)
    p.add_argument("--ui", required=True, type=Path)
    p.add_argument("--display", required=True, type=Path)
    p.add_argument("--strip", required=True)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError("Output must be new")
    rows = [line.split("\t") for line in (a.baseline / "runtime.tsv").read_text().splitlines()]
    if len(rows) != 1648 or any(len(row) != 4 for row in rows):
        raise ValueError("Unexpected baseline manifest")
    entries = {row[2]: row for row in rows if row[0] in ("F", "C")}
    guards = {
        "usr/bin/lilsmoke-ui": "b948f3fec73eb27116a905241b7056240548717af530498902667b2bfae4188a",
        "usr/lib/libfusion-1.7.so.7.0.0": "de585deb69ab6f8a3f52abcf7cba1d53a032d50509c2d84177753f1e7764200e",
        "usr/lib/libdirect-1.7.so.7.0.0": "dc31891cf6c3348ca19b0f6224a062d1302a946fe9a7f5358c91b6b9d989e52b",
        "usr/bin/retroarch": "5e12a6d7a35dea5bfc425b2cf8e2e25595826837de91909ced2a2f8d4b8ca4fa",
    }
    for path, digest in guards.items():
        if path not in entries or sha(a.baseline / "files" / entries[path][3]) != digest:
            raise ValueError("Qualified baseline guard failed: " + path)
    before = {path: sha(a.baseline / "files" / row[3]) for path, row in entries.items()}
    shutil.copytree(a.baseline, a.output)
    ui = a.output / "files" / entries["usr/bin/lilsmoke-ui"][3]
    subprocess.run([a.strip, "-o", str(ui), str(a.ui)], check=True)
    shutil.copyfile(a.display, a.output / "libm8-display.so")
    after = {path: sha(a.output / "files" / row[3]) for path, row in entries.items()}
    changed = [path for path in before if before[path] != after[path]]
    if changed != ["usr/bin/lilsmoke-ui"]:
        raise ValueError("Unexpected runtime changes: " + repr(changed))
    report = {"baseline": str(a.baseline), "objects": len(entries), "changed_objects": changed,
              "ui_sha256": after[changed[0]], "display_sha256": sha(a.output / "libm8-display.so"),
              "unchanged_guards": {k: after[k] for k in guards if k not in changed},
              "physical_smoothness_verified": False}
    (a.output.parent / "stage-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
