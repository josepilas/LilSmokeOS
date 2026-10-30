#!/usr/bin/env python3
"""Prepare PC tools under a NEW local LILOS staging folder. No SD writes."""
import argparse
from pathlib import Path
import shutil
import adapter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    output = adapter.safe_absolute(args.destination, False)
    if output.exists():
        raise adapter.Refusal("Delivery destination must be new")
    output.mkdir(parents=True)
    common = output / "PC-Tools/LilSmoke Device Adapter"
    common.mkdir(parents=True)
    for name in ("adapter.py", "gui.py", "launch.py", "LICENSE", "README.md",
                 "LilSmoke Device Adapter.cmd", "LilSmoke Device Adapter.sh", "LilSmoke Device Adapter.command"):
        shutil.copyfile(source / name, common / name)
    shutil.copytree(source / "profiles", common / "profiles")
    shutil.copytree(source / "third_party", common / "third_party")
    shutil.copyfile(source / "release/windows-final/LilSmoke Device Adapter.exe", common / "LilSmoke Device Adapter.exe")
    for name in ("LilSmoke Device Adapter.sh", "LilSmoke Device Adapter.command"):
        (common / name).chmod(0o755)
    manifest = {p.relative_to(output).as_posix(): dict(sha256=adapter.hash_file(p)[0], bytes=p.stat().st_size)
                for p in sorted(output.rglob("*")) if p.is_file()}
    adapter.write_json(output / "delivery.json", dict(schema=1, files=manifest, file_count=len(manifest),
                       bytes=sum(r["bytes"] for r in manifest.values()), proprietary_firmware_included=False,
                       windows_frozen=True, linux_macos_python_required=True, sd_written=False))
    print(output / "delivery.json")


if __name__ == "__main__":
    main()
