#!/usr/bin/env python3
"""Stage the existing 0001-0004 build, check new patches and run focused tests.

Run in WSL with --source pointing to Buildroot's PocketSNES source. Never
modifies that source, target/, a mounted card, or the original launcher.
"""
import argparse
import difflib
import os
import pathlib
import re
import shutil
import subprocess


def recount_patch(path):
    # Unified-diff formatting only; do not change any source/content lines.
    lines = path.read_text().splitlines(keepends=True)
    for index, line in enumerate(lines):
        match = re.match(r"@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)", line)
        if not match:
            continue
        old = new = 0
        for content in lines[index + 1:]:
            if content.startswith(("@@ ", "--- a/", "diff ")):
                break
            if content.startswith((" ", "-")):
                old += 1
            if content.startswith((" ", "+")):
                new += 1
        lines[index] = f"@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n"
    path.write_text("".join(lines))


def canonicalize_patches(patches, source):
    # Format hand-authored hunks as canonical, balanced-context unified diffs.
    files = {}
    for patch in patches:
        text = patch.read_text()
        header, *sections = re.split(r"(?m)^--- a/", text)
        output = header
        for section in sections:
            name = section.splitlines()[0]
            original = files.get(name)
            if original is None:
                original = (source / name).read_text(encoding="latin-1").splitlines(keepends=True)
            changed = original[:]
            cursor = 0
            for block in re.split(r"(?m)^@@[^\n]*\n", section)[1:]:
                lines = block.splitlines(keepends=True)
                old = [line[1:] for line in lines if line.startswith((" ", "-"))]
                new = [line[1:] for line in lines if line.startswith((" ", "+"))]
                positions = [i for i in range(cursor, len(changed) - len(old) + 1)
                             if changed[i:i + len(old)] == old]
                if not positions:
                    positions = [i for i in range(len(changed) - len(old) + 1)
                                 if changed[i:i + len(old)] == old]
                if not positions:
                    raise ValueError(f"Hunk does not match {name}: {old[:3]}")
                position = positions[0]
                changed[position:position + len(old)] = new
                cursor = position + len(new)
            output += "".join(difflib.unified_diff(original, changed, fromfile=f"a/{name}", tofile=f"b/{name}"))
            files[name] = changed
        patch.write_text(output)


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    environment = os.environ.copy()
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    kwargs.setdefault("env", environment)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path)
    parser.add_argument("--buildroot-output", type=pathlib.Path)
    parser.add_argument("--format-patches", action="store_true")
    parser.add_argument("--stage-name", default="stage")
    parser.add_argument("--baseline-only", action="store_true")
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    package = repo / "package/opendingux-emulators"
    patches = sorted(package.glob("000[56]-*.patch"))
    if args.format_patches:
        if args.source:
            canonicalize_patches(patches, args.source)
        else:
            for patch in patches:
                recount_patch(patch)
        return
    if not args.source or not args.buildroot_output:
        parser.error("--source and --buildroot-output are required")
    results = repo / "test-results/m8-snes-2026-10-07"
    results.mkdir(parents=True, exist_ok=True)
    if not re.fullmatch(r"stage[-a-z0-9]*", args.stage_name):
        parser.error("stage name must be a simple stage-prefixed directory name")
    stage = results / args.stage_name
    if stage.exists():
        parser.error("stage already exists; choose a fresh stage before reapplying patches")
    if args.source.is_file():
        stage.mkdir()
        run(["tar", "--strip-components=1", "-xzf", args.source, "-C", stage])
        patches = sorted(package.glob("000[1-9]-*.patch"))
    else:
        shutil.copytree(args.source, stage, ignore=shutil.ignore_patterns("*.o", "*.dge", ".stamp*"))
    if args.baseline_only:
        patches = [path for path in patches if path.name[:4] in ("0001", "0002", "0003", "0004")]
    for patch in patches:
        run(["patch", "--batch", "--fuzz=0", "-p1", "-i", patch], cwd=stage)
    if args.baseline_only:
        return
    sysroot = args.buildroot_output / "staging"
    test = results / "pocketsnes-host-test"
    run(["cc", "-std=gnu99", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-D__DINGUX__",
         f"-I{package}", f"-I{stage}/sal/linux/include", f"-I{stage}/sal/include",
         f"-I{sysroot}/usr/include/SDL", repo / "tools/m8/m8-pocketsnes-test.c", "-o", test])
    run([test, repo / "tools/m8/m8-pocketsnes-test.map"])
    host = args.buildroot_output / "host/bin"
    run(["make", "-j2", "-f", package / "Makefile.lilsmoke",
         f"CC={host}/mipsel-linux-gcc", f"CXX={host}/mipsel-linux-g++",
         "TARGET_CFLAGS=-march=mips32r2 -mabi=32 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -mlong-calls",
         f"SDL_CFLAGS=-I{sysroot}/usr/include/SDL",
         f"SDL_LIBS=-L{sysroot}/usr/lib -lSDL"], cwd=stage)
    run([host / "mipsel-linux-size", stage / "pocketsnes/pocketsnes.dge"])
    run([host / "mipsel-linux-readelf", "-A", stage / "pocketsnes/pocketsnes.dge"])


if __name__ == "__main__":
    main()
