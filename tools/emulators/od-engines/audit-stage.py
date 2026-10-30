#!/usr/bin/env python3
"""Inventory real staged ELFs and their SDK dependency closure, without deployment."""
import argparse
import hashlib
import json
import re
import subprocess
import shutil
from pathlib import Path


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def elf_info(readelf, path):
    result = subprocess.run(
        [str(readelf), "-h", "-A", "-d", str(path)],
        check=True, text=True, capture_output=True,
    ).stdout
    if "ELF32" not in result or "little endian" not in result or "MIPS" not in result:
        raise ValueError(f"Not a target MIPS little-endian ELF32: {path}")
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", result)
    if any("/" in name or "qa" in name.lower() for name in needed):
        raise ValueError(f"Unsafe or QA dependency: {path}: {needed}")
    return {"bytes": path.stat().st_size, "sha256": digest(path), "needed": needed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("/root/lilsmoke-emulator-expansion-20261008"))
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output/host"))
    parser.add_argument("--copy-libraries", action="store_true")
    args = parser.parse_args()
    stage = args.root / "built-native"
    sysroot = args.root / "engine-sysroot"
    readelf = args.sdk / "bin/mipsel-buildroot-linux-musl-readelf"
    manifest = {"schemaVersion": 1, "hardwareAcceptance": False, "binaries": {},
                "libraries": {}, "assets": {}, "missing": [],
                "scope": "Production stage only; NX candidates and all QA artifacts excluded.",
                "limitations": "DT_NEEDED closure only. SDL/DirectFB runtime-loaded modules and hardware display remain parent-owned validation."}
    files = [stage / "libod-sdl.so", *sorted((stage / "bin").glob("*"))]
    pending = []
    for path in files:
        info = elf_info(readelf, path)
        manifest["binaries"][str(path.relative_to(stage))] = info
        pending.extend(info["needed"])
    while pending:
        name = pending.pop()
        if name in manifest["libraries"] or name in manifest["missing"]:
            continue
        candidates = [stage / "lib" / name, sysroot / "usr/lib" / name, sysroot / "lib" / name]
        path = next((p.resolve() for p in candidates if p.is_file()), None)
        if path is None or not (path.is_relative_to(sysroot.resolve()) or path.is_relative_to(stage.resolve())):
            manifest["missing"].append(name)
            continue
        info = elf_info(readelf, path)
        info["source"] = str(path)
        manifest["libraries"][name] = info
        if args.copy_libraries:
            base=args.sdk.parent/"target/usr/lib"/name
            if not base.is_file():base=args.sdk.parent/"target/lib"/name
            if not base.is_file() or digest(base)!=info["sha256"]:
                destination=stage/"lib"/name
                destination.parent.mkdir(parents=True,exist_ok=True)
                if destination.resolve()!=path:shutil.copyfile(path,destination)
        pending.extend(info["needed"])
    for directory in (stage / "data", stage / "licenses"):
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                manifest["assets"][str(path.relative_to(stage))] = {
                    "bytes": path.stat().st_size, "sha256": digest(path)}
    output = stage / "stage-manifest.json"
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    print(f"Audited {len(files)} production ELFs, {len(manifest['libraries'])} libraries, "
          f"{len(manifest['assets'])} assets. Missing: {manifest['missing']}")
    print(output)
    if manifest["missing"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
