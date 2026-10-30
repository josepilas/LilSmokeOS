#!/usr/bin/env python3
"""Read-only FDT fingerprints; uses the standard dtc fdtget parser."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

def query(blob, arguments):
    result = subprocess.run(["fdtget", *arguments[:2], str(blob), *arguments[2:]],
                            text=True, capture_output=True)
    return result.stdout.strip() if result.returncode == 0 else None

def read(blob, node, prop, kind="x"):
    return query(blob, ["-t", kind, node, prop])

def children(blob, node):
    p = subprocess.run(["fdtget", "-l", str(blob), node], text=True, capture_output=True)
    return p.stdout.splitlines() if p.returncode == 0 else []

def fingerprint(blob):
    nodes = []
    queue = ["/"]
    while queue:
        node = queue.pop()
        nodes.append(node)
        queue.extend(node.rstrip("/") + "/" + n for n in children(blob, node))
    selected = {}
    for node in sorted(nodes):
        if not re.search(r"(board$|memory($|@|-mapping/)|reserved-memory|cpu[s/@]|clock|rotate$|"
                         r"dsi|lcd|panel|hdmi|display|fb|sound|audio|i2s|usb|dwc|mmc|sdmmc|"
                         r"thermal|gpio-keys|serializer-keys|queryadc|external_files$)", node):
            continue
        p = subprocess.run(["fdtget", "-p", str(blob), node], text=True, capture_output=True)
        values = {}
        for prop in p.stdout.splitlines():
            kind = "s" if prop in {"label", "model", "compatible", "status", "device_type",
                                  "path-prefix", "clock-names", "pinctrl-names", "dr_mode",
                                  "reg-names", "phy-names"} or prop.endswith("-filename") else "x"
            values[prop] = read(blob, node, prop, kind)
        selected[node] = values
    return {"file": blob.name, "sha256": hashlib.sha256(blob.read_bytes()).hexdigest(),
            "bytes": blob.stat().st_size, "model": read(blob, "/", "model", "s"),
            "compatible": read(blob, "/", "compatible", "s"), "nodes": selected}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dtbs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps([fingerprint(p) for p in args.dtbs], indent=2) + "\n")

if __name__ == "__main__":
    main()
