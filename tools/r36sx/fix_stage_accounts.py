#!/usr/bin/env python3
"""Normalize only known generated locked accounts in an owned unassembled stage."""
import argparse
import hashlib
import json
from pathlib import Path
from build_candidate import ARCHIVE_SHA256, LOCKED_ACCOUNTS

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("stage", type=Path)
a = p.parse_args()
report_file = a.stage / "stage.json"
report = json.loads(report_file.read_text())
if report["archive_sha256"] != ARCHIVE_SHA256 or report["physical_boot_verified"]:
    raise ValueError("Not the owned unverified stock-lineage stage")
for name, contents in LOCKED_ACCOUNTS.items():
    relative = "rootfs/etc/" + name
    file = a.stage / "LILOS" / relative
    old = file.read_bytes()
    if old.replace(b"\r\n", b"\n") != contents.encode("ascii"):
        raise ValueError("Refusing to overwrite a customized account file")
    if hashlib.sha256(old).hexdigest() != report["os_files"][relative]["sha256"]:
        raise ValueError("Stage was changed outside the builder")
for name, contents in LOCKED_ACCOUNTS.items():
    relative = "rootfs/etc/" + name
    data = contents.encode("ascii")
    (a.stage / "LILOS" / relative).write_bytes(data)
    report["os_files"][relative] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
report["generated_account_line_endings"] = "LF"
report_file.write_text(json.dumps(report, indent=2) + "\n")
print("PASS normalized four known generated locked accounts only; updated exact artifact hashes")
