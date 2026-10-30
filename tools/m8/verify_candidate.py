#!/usr/bin/env python3
"""Read-only verification of a private M8 candidate against its source backup."""
import argparse
import json
from pathlib import Path
import tempfile
from stock_format import inspect, uimage
from build_stock_candidate import BOOT_FILES, run, sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--stock-image", type=Path, required=True)
    a = p.parse_args()
    built, stock = inspect(a.image), inspect(a.stock_image)
    if len(built["partitions"]) != 2 or [v["label"] for v in built["partitions"]] != ["LILOS", "LILROMS"]:
        raise ValueError("candidate partition contract is invalid")
    if built["decoded_mbr"][:446] != stock["decoded_mbr"][:446] or \
            built["prefix"][512:] != stock["prefix"][512:]:
        raise ValueError("vendor boot-region bytes outside partition table changed")
    volume = f"{a.image}@@1048576"
    with tempfile.TemporaryDirectory(prefix="m8-verify-", dir="/root") as temp:
        work = Path(temp)
        run(["mcopy", "-i", volume, "::/lilsmoke/candidate.json", work / "candidate.json"])
        report = json.loads((work / "candidate.json").read_text())
        if sha(a.stock_image) != report["source_img_sha256"]:
            raise ValueError("source backup hash does not match candidate provenance")
        if built["encoding"] != report["encoding"]:
            raise ValueError("MBR encoding differs from candidate contract")
        for name in BOOT_FILES:
            saved = "icube.stock" if name == "icube" else name
            run(["mcopy", "-i", volume, "::/cubegm/" + saved, work / saved])
            if sha(work / saved) != report["stock_hashes"][name]:
                raise ValueError("stock boot dependency mismatch: " + name)
            if name.endswith((".uImage", ".dll")): uimage(work / saved)
        for name, expected in report.get("required_vendor_libs", {}).items():
            if name != "usr/lib/libhcfota.so":
                raise ValueError("unexpected vendor-library path in candidate contract")
            run(["mcopy", "-i", volume, "::/cubegm/" + name, work / "required-lib"])
            if sha(work / "required-lib") != expected:
                raise ValueError("required vendor dependency mismatch: " + name)
        run(["mcopy", "-i", volume, "::/lilsmoke/runtime.tsv", work / "runtime.tsv"])
        rows = [line.split("\t") for line in (work / "runtime.tsv").read_text().splitlines()]
        if len(rows) != report["runtime"]["records"]:
            raise ValueError("runtime manifest record count differs")
        ui = next(row for row in rows if row[2] == "usr/bin/lilsmoke-ui")
        run(["mcopy", "-i", volume, "::/lilsmoke/files/" + ui[3], work / "ui"])
        with (work / "ui").open("rb") as stream: elf = stream.read(64)
        if elf[:6] != b"\x7fELF\x01\x01" or elf[18:20] != b"\x08\x00":
            raise ValueError("UI is not native ELF32 little-endian MIPS")
    report.update(output=str(a.image.resolve()), image_sha256=sha(a.image), source_unchanged=True,
                  verified_boot_dependencies=True, verified_native_ui=True, partitions=built["partitions"])
    a.image.with_suffix(".img.json").write_text(json.dumps(report, indent=2) + "\n")
    a.image.with_suffix(".img.sha256").write_text(report["image_sha256"] + "  " + a.image.name + "\n")
    print(json.dumps({k: report[k] for k in ("output", "image_sha256", "source_unchanged", "verified_boot_dependencies", "physical_boot_verified", "runtime", "partitions")}, indent=2))


if __name__ == "__main__": main()
