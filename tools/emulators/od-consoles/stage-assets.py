#!/usr/bin/env python3
"""Stage only explicitly permitted UI assets, never upstream ROMs or BIOS."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def stage_assets(engine, source, stage, package):
    spec = json.loads((package / "assets.json").read_text())["engines"].get(engine)
    if spec is None:
        raise RuntimeError("no reviewed asset whitelist: " + engine)
    destination = stage / "storage/emulators/share" / engine
    destination.mkdir(parents=True, exist_ok=True)
    inputs = [(source / name, Path(name).name) for name in spec.get("files", [])]
    for tree, extensions in spec.get("trees", {}).items():
        root = source / tree
        if not root.is_dir():
            raise RuntimeError("asset directory absent: " + str(root))
        for item in sorted(root.rglob("*")):
            if item.is_file() and item.suffix.lower() in extensions:
                inputs.append((item, str(Path(root.name) / item.relative_to(root))))
    records = []
    for item, relative in inputs:
        if item.is_symlink() or not item.is_file() or source.resolve() not in item.resolve().parents:
            raise RuntimeError("asset absent or outside pinned source: " + str(item))
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, output)
        records.append(dict(path=relative, bytes=output.stat().st_size,
                            sha256=hashlib.sha256(output.read_bytes()).hexdigest()))
    report = dict(engine=engine, directory=str(destination), files=records, biosBundled=False)
    (destination / "assets-manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("engines", nargs="+")
    parser.add_argument("--cache", type=Path, default=Path("/root/lilsmoke-emulator-expansion-20261008"))
    args = parser.parse_args()
    package = Path(__file__).resolve().parents[3] / "package/lilsmoke-od-consoles"
    sources = json.loads((package / "sources.json").read_text())["engines"]
    for engine in args.engines:
        entry = sources[engine]
        source = args.cache / "od-console-src" / entry.get("source_directory", engine)
        print(json.dumps(stage_assets(engine, source, args.cache / "od-console-stage", package)))


if __name__ == "__main__":
    main()
