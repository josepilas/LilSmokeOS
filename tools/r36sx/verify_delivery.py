#!/usr/bin/env python3
"""Verify public Pixeldrain metadata before optionally deleting one built image."""
# SPDX-License-Identifier: MPL-2.0
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
RELEASE = ROOT / "build/r36sx-2.7-20261023-diagnostics/release"


def validate(image, report, server, file_id):
    if image.is_symlink() or not image.is_file() or image.suffix != ".img" or image.resolve().parent != RELEASE.resolve():
        raise ValueError("Only one regular image in this task's release directory is allowed")
    if (not report.get("image_generated") or not report.get("protected_files_unchanged") or
            not report.get("image_readback_protected_sha256") or report.get("physical_boot_verified") or
            report.get("diagnostic_revision") != 2 or report.get("profile") != "r36sx-2.7-owner-backup-20261022"):
        raise ValueError("Expected a qualified file image with hardware status still unverified")
    if not re.fullmatch(r"[0-9a-f]{64}", report["image_sha256"]):
        raise ValueError("Invalid build digest")
    if (not server.get("success") or server.get("id") != file_id or server.get("name") != image.name or
            server.get("size") != report["image_bytes"] or image.stat().st_size != report["image_bytes"] or
            server.get("hash_sha256") != report["image_sha256"]):
        raise ValueError("Upload name/size/full-file SHA-256 does not match; keep local image")
    checksum = hashlib.sha256()
    with image.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    if checksum.hexdigest() != report["image_sha256"]:
        raise ValueError("Local image changed; do not delete")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("file_id")
    parser.add_argument("--delete-local", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{8,16}", args.file_id):
        parser.error("Invalid observed Pixeldrain file ID")
    report = json.loads(args.image.with_suffix(".img.json").read_text())
    url = "https://pixeldrain.com/api/file/" + args.file_id + "/info"
    with urlopen(Request(url, headers={"User-Agent": "LilSmokeOS-delivery-verification"}), timeout=45) as response:
        raw = response.read(65537)
    if len(raw) > 65536:
        raise ValueError("Unexpected metadata size")
    server = json.loads(raw)
    validate(args.image, report, server, args.file_id)
    receipt = {"verified_at_utc": datetime.now(timezone.utc).isoformat(),
               "url": "https://pixeldrain.com/u/" + args.file_id,
               "image_name": args.image.name, "bytes": report["image_bytes"],
               "sha256": report["image_sha256"], "matching_upload": True,
               "local_image_deleted": False, "physical_boot_verified": False,
               "connected_segam_card_modified": False}
    args.image.parent.joinpath("pixeldrain-info-verified.json").write_text(json.dumps(server, indent=2) + "\n")
    output = args.image.parent / "delivery-receipt.json"
    output.write_text(json.dumps(receipt, indent=2) + "\n")
    if args.delete_local:
        args.image.unlink()  # Single verified regular file, never recursive.
        receipt["local_image_deleted"] = not args.image.exists()
        output.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
