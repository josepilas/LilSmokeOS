#!/usr/bin/env python3
"""Maintainer-only read-only hash profile build; never ships stock file bytes."""
import argparse
import json
from pathlib import Path
import sys
import adapter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("--output", default=str(Path(__file__).parent / "profiles/segam-m8-v6.json"))
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(project / "tools/m8"))
    # Reuse the existing allowlist/parser read-only, without changing its source.
    from check_stock_directory import check_directory, import_files, file_hash
    checked = check_directory(args.source)
    if not checked["passed"]:
        raise adapter.Refusal("Stock gate hashes do not match the project reference")
    files = {}
    for name, path in sorted(import_files(Path(checked["stock_directory"])).items()):
        value, size = file_hash(path)
        if size:
            files[name] = dict(sha256=value, bytes=size)
    profile = dict(schema=1, id="segam-m8-v6-owner-stock-2024-12-05", files=files,
                   reference_image_sha256=checked["reference_source_img_sha256"],
                   launch_sha256=[adapter.hash_file(project / "board/segam-m8-v6-stock/m8-launch.sh")[0]],
                   evidence="Exact hashes from the owner's read-only recovered stock folder, gated by Candidate06. "
                            "This lineage physically boots LilSmoke on the owner's M8. Other boards are not certified.",
                   boot_route="Kernel S99app -> /mnt/sdcard/cubegm/icube -> lilsmoke/m8-launch.sh",
                   reserved_region="Not exported. Target must already be flashed with this compatible base.",
                   source_reference="build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-06.img.json")
    output = adapter.safe_absolute(args.output, False)
    output.parent.mkdir(parents=True, exist_ok=True)
    adapter.write_json(output, profile)
    print(json.dumps(dict(files=len(files), bytes=sum(r["bytes"] for r in files.values()),
                         profile=str(output), proprietary_bytes_in_profile=0), indent=2))


if __name__ == "__main__":
    main()
