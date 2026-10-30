#!/usr/bin/env python3
"""Rebuild a manifested VM fixture after C++-only changes, preserving the old run."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

P = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--previous", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    old = json.loads((a.previous / "sources-manifest.json").read_text())
    if sha(a.previous / "themed-power") != old["binary_sha256"] or a.output.exists():
        raise ValueError("Invalid previous binary or existing output")
    old_source = Path(old["source_snapshot"])
    changed = []
    for item in old["inputs"]:
        source = Path(item["path"])
        if source.is_relative_to(old_source):
            live = P / source.relative_to(old_source)
            if sha(live) != item["sha256"]:
                if source.suffix != ".cpp":
                    raise ValueError("A header changed; complete rebuild required")
                changed.append(source.relative_to(old_source))
        elif sha(source) != item["sha256"]:
            raise ValueError("SDK dependency changed")
    a.output.mkdir(parents=True)
    snapshot = a.output / "source-snapshot"
    shutil.copytree(old_source, snapshot)
    shutil.copytree(a.previous / "objects", a.output / "objects")
    commands = [[part.replace(str(a.previous), str(a.output)) for part in command] for command in old["commands"]]
    for relative in changed:
        shutil.copyfile(P / relative, snapshot / relative)
    with (a.output / "build.log").open("w") as log:
        for command in commands[:-1]:
            src = Path(command[command.index("-c") + 1])
            if src.relative_to(snapshot) in changed:
                print("Building", src.name, flush=True)
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        subprocess.run(commands[-1], check=True, stdout=log, stderr=subprocess.STDOUT)
    inputs = []
    for item in old["inputs"]:
        path = Path(item["path"])
        if path.is_relative_to(old_source):
            path = snapshot / path.relative_to(old_source)
        inputs.append(dict(path=str(path), sha256=sha(path)))
    old.update(commands=commands, inputs=inputs, source_snapshot=str(snapshot),
               binary_sha256=sha(a.output / "themed-power"), reused_build=str(a.previous))
    (a.output / "sources-manifest.json").write_text(json.dumps(old, indent=2) + "\n")
    print("MANIFESTED_VM_REBUILD_OK changed=" + str(changed))


if __name__ == "__main__":
    main()
