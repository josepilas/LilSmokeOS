#!/usr/bin/env python3
"""Rebuild changed C++ files only, retaining a verified prior build for rollback."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

P = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--previous", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--sdk", type=Path, default=Path("/root/lilsmoke-os/build/buildroot/output"))
    a = parser.parse_args()
    old = a.previous / "source-inputs.after"
    if old.read_bytes() != (a.previous / "source-inputs.before").read_bytes():
        raise ValueError("Previous build did not qualify")
    if a.output.exists():
        raise ValueError("Output must be new")
    before = {Path(line[66:]): line[:64] for line in old.read_text().splitlines()}
    paths = sorted(path for directory in ("lilsmoke-ui/src/src", "lilsmoke-od-sdl/src",
                   "lilsmoke-media/src", "lilsmoke-scripts/src")
                   for path in (P / "package" / directory).rglob("*") if path.suffix in (".h", ".cpp"))
    hashes = {path: sha(path) for path in paths}
    header_change = any(before.get(path) != digest for path, digest in hashes.items() if path.suffix == ".h")
    a.output.mkdir(parents=True)
    build = a.output / "build"
    build.mkdir()
    snapshot = "".join(digest + "  " + str(path) + "\n" for path, digest in hashes.items())
    (a.output / "source-inputs.before").write_text(snapshot)
    source = P / "package/lilsmoke-ui/src"
    names = subprocess.check_output(["make", "--no-print-directory", "-s", "-C", str(source),
        "--eval=print-sources:;@echo $(SRCS)", "print-sources"], text=True).split()
    cxx = str(a.sdk / "host/bin/mipsel-buildroot-linux-musl-g++")
    flags = ["-std=c++17", "-O3", "-flto", "-Wall", "-Wextra", "-Wno-unused-parameter",
             "-ffunction-sections", "-fdata-sections", "-include",
             str(P / "package/lilsmoke-od-sdl/src/input-wire.h"),
             "-I" + str(P / "package/lilsmoke-scripts/src"),
             "-I" + str(P / "package/lilsmoke-media/src"),
             "-I" + str(a.sdk / "staging/usr/include"),
             "-I" + str(a.sdk / "staging/usr/include/SDL2")]
    objects = []
    with (a.output / "build.log").open("w") as log:
        for name in names:
            path = source / name
            obj = build / (path.stem + ".o")
            previous = a.previous / "build" / obj.name
            reuse = not header_change and before.get(path) == hashes.get(path) and previous.is_file()
            if reuse:
                shutil.copyfile(previous, obj)
            else:
                print("Building", path.name, flush=True)
                subprocess.run([cxx, *flags, "-c", str(path), "-o", str(obj)],
                               check=True, stdout=log, stderr=subprocess.STDOUT)
            objects.append(str(obj))
        subprocess.run([cxx, "-O3", "-flto", "-Wl,--gc-sections", *objects,
                        "-lSDL2_image", "-lSDL2_ttf", "-lSDL2", "-lpng", "-ljson-c", "-lz", "-lwebp",
                        "-o", str(build / "lilsmoke-ui")], check=True, stdout=log, stderr=subprocess.STDOUT)
    if any(sha(path) != digest for path, digest in hashes.items()):
        raise ValueError("Sources changed during rebuild")
    (a.output / "source-inputs.after").write_text(snapshot)
    print("QUALIFIED_INCREMENTAL_UI_OK sha256=" + sha(build / "lilsmoke-ui"))


if __name__ == "__main__":
    main()
