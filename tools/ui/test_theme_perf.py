#!/usr/bin/env python3
"""Profile actual production software views; use the same frames for both sources."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

from test_theme_motion import THEMES, snapshot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--themes", default="/root/lilsmoke-vm-validation/storage/themes")
    parser.add_argument("--compare")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--timing-only", action="store_true", help="Remove decode/TTF/copy instrumentation for timing")
    parser.add_argument("--sizes", default="856x480,1280x720")
    args = parser.parse_args()
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    source = out / "source"
    snapshot(argparse.Namespace(source=args.source, output=source, manifest=out / "sources.json"))
    harness = Path(__file__).with_name("theme-perf.cpp")
    (out / "harness.sha256").write_text(hashlib.sha256(harness.read_bytes()).hexdigest() + "\n")
    flags = ["-std=c++17", "-O3", "-g", "-Wall", "-Wextra", "-Werror", "-fno-omit-frame-pointer", "-rdynamic"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
    flags += ["-I" + str(source)]
    flags += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
    inputs = [harness] + [source / (n + ".cpp") for n in
        ("SystemView", "GridView", "Renderer", "Font", "Theme", "ThemeRender", "Log")]
    libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl2", "SDL2_image", "SDL2_ttf"], text=True))
    wraps = ["SDL_GetTicks", "IMG_Load", "TTF_SizeUTF8", "TTF_RenderUTF8_Blended", "SDL_RenderCopy", "SDL_RenderCopyEx"]
    if args.timing_only:
        flags += ["-DPERF_TIMING_ONLY"]
        wraps = ["SDL_GetTicks"]
    command = ["g++", *flags, *map(str, inputs), *libs, "-ldl", *["-Wl,--wrap=" + w for w in wraps], "-o", str(out / "theme-perf")]
    (out / "build-command.json").write_text(json.dumps(command, indent=2))
    with (out / "build.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    results = {}
    for size in args.sizes.split(","):
        for name, dirname in THEMES.items():
            key = name + "-" + size
            print(key, flush=True)
            with (out / (key + ".tsv")).open("w") as data, (out / (key + ".log")).open("w") as log:
                subprocess.run([str(out / "theme-perf"), str(Path(args.themes) / dirname),
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", *size.split("x")],
                    stdout=data, stderr=log, check=True, timeout=300,
                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
            # The logger writes to stdout too; retain only the tab-separated harness rows.
            lines = [line for line in (out / (key + ".tsv")).read_text().splitlines() if "\t" in line]
            rows = list(csv.DictReader(lines, delimiter="\t"))
            if args.compare:
                old_lines = [line for line in (Path(args.compare) / (key + ".tsv")).read_text().splitlines() if "\t" in line]
                old = list(csv.DictReader(old_lines, delimiter="\t"))
                assert [(r["phase"], r["frame"], r["time"], r["hash"]) for r in rows] == [
                    (r["phase"], r["frame"], r["time"], r["hash"]) for r in old], key + " pixel regression"
            stats = {}
            for phase in ("cold", "idle", "system", "grid"):
                part = [r for r in rows if r["phase"] == phase]
                stats[phase] = {field: round(sum(float(r[field]) for r in part) / len(part), 6) for field in
                    ("render_ms", "present_ms", "compare_ms", "decodes", "decode_ms", "measures", "measure_ms", "rasters", "raster_ms", "scaled")}
                stats[phase]["p95_render_ms"] = sorted(float(r["render_ms"]) for r in part)[min(len(part)-1, int(len(part)*.95))]
            stats["max_image_bytes"] = max(int(r["image_bytes"]) for r in rows)
            stats["max_text_bytes"] = max(int(r["text_bytes"]) for r in rows)
            stats["frames"] = len(rows)
            results[key] = stats
            (out / "report.json").write_text(json.dumps({"kind": "host software, not M8 FPS", "instrumented": not args.timing_only, "pixel_identical_to": args.compare,
                "results": results}, indent=2) + "\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
