#!/usr/bin/env python3
"""Same production views and pixels, including Applications, on host/SDK SDL."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import statistics
from PIL import Image
from test_theme_motion import THEMES, snapshot


def build_cached(cxx, flags, sources, libs, out, log):
    """Persist each completed object so a power interruption loses one compile."""
    objects = out / "objects"
    objects.mkdir(exist_ok=True)
    headers = b"".join(p.read_bytes() for p in sorted((out / "source").glob("*.h")))
    harnesses = b"".join(p.read_bytes() for p in sorted(Path(__file__).parent.glob("candidate41*.cpp")))
    common = json.dumps([cxx, flags]).encode() + headers + harnesses
    files = []
    for source in sources:
        target = objects / (source.stem + ".o")
        stamp = target.with_suffix(".sha256")
        digest = hashlib.sha256(common + source.read_bytes()).hexdigest()
        if not target.exists() or not stamp.exists() or stamp.read_text() != digest:
            temporary = target.with_suffix(".tmp.o")
            command = [cxx, *flags, "-c", str(source), "-o", str(temporary)]
            log.write(shlex.join(command) + "\n"); log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            temporary.replace(target); stamp.write_text(digest)
        files.append(str(target))
    command = [cxx, *files, *libs,
        "-Wl,--wrap=SDL_GetTicks,--wrap=IMG_Load,--wrap=SDL_GetPerformanceCounter,--wrap=SDL_GetPerformanceFrequency",
        *(["-fsanitize=address,undefined", "-no-pie"] if "-fsanitize=address,undefined" in flags else []),
        "-o", str(out / "perf.tmp")]
    log.write(shlex.join(command) + "\n"); log.flush()
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / "perf.tmp").replace(out / "perf")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--compare")
    p.add_argument("--mips", action="store_true")
    p.add_argument("--sanitize", action="store_true")
    p.add_argument("--short", action="store_true")
    p.add_argument("--units", action="store_true")
    p.add_argument("--build-only", action="store_true")
    p.add_argument("--sizes", default="856x480,1280x720")
    p.add_argument("--themes", default=",".join(THEMES))
    a = p.parse_args()
    out = Path(a.output); out.mkdir(parents=True, exist_ok=True)
    snapshot(argparse.Namespace(source=a.source, output=out / "source", manifest=out / "sources.json"))
    s = out / "source"
    harness = Path(__file__).with_name("candidate41-renderer-unit.cpp" if a.units else "candidate41-perf.cpp")
    flags = ["-std=c++17", "-O3", "-g", "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror", "-I" + str(s),
        "-I" + str(Path(__file__).resolve().parents[2] / "package/lilsmoke-scripts/src")]
    sources = [harness] + [s / (n + ".cpp") for n in
        ("Renderer", "Font", "Theme", "ThemeRender", "SystemView", "GridView", "Log", "ApplicationsView", "ApplicationArtwork", "ProcessLaunch")]
    sdk = Path("/root/lilsmoke-os/build/buildroot/output")
    libs = ["-lSDL2_image", "-lSDL2_ttf", "-lSDL2", "-lwebp", "-ljson-c", "-ldl"]
    if a.mips:
        cxx = str(sdk / "host/bin/mipsel-buildroot-linux-musl-g++")
        flags += ["-I" + str(sdk / "staging/usr/include")]
    else:
        cxx = "g++"
        flags += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl2", "SDL2_image", "SDL2_ttf", "json-c"], text=True))
    if a.sanitize:
        flags += ["-O1", "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
    command = [cxx, *flags, *map(str, sources), *libs,
        "-Wl,--wrap=SDL_GetTicks,--wrap=IMG_Load,--wrap=SDL_GetPerformanceCounter,--wrap=SDL_GetPerformanceFrequency", "-o", str(out / "perf")]
    (out / "build-command.json").write_text(json.dumps(command, indent=2))
    (out / "harness.sha256").write_text(hashlib.sha256(harness.read_bytes()).hexdigest())
    with (out / "build.log").open("w") as log:
        build_cached(cxx, flags, sources, libs, out, log)
    if a.units:
        command = [str(out / "perf"), str(out / "fixtures")]
        if a.mips: command = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(sdk / "target"), *command]
        subprocess.run(command, check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
        return
    icon = out / "icon.png"
    im = Image.new("RGBA", (96, 80))
    im.putdata([((x * 3) % 256, (y * 5) % 256, (x + y) % 256, (x * 7 + y * 3) % 256)
        for y in range(80) for x in range(96)])
    im.save(icon)
    if a.build_only: return
    report = {"kind": "MIPS QEMU SDK SDL" if a.mips else "host SDL software", "not_hardware_fps": True,
        "asset_cache_mb": int(os.environ.get("C41_CACHE_MB", "8")),
        "timing_clock": "thread CPU" if "C41_CPU_CLOCK" in os.environ else "wall",
        "pixel_identical_to": a.compare, "results": {}}
    for size in a.sizes.split(","):
        for slug in a.themes.split(","):
            key = slug + "-" + size; print(key, flush=True)
            command = [str(out / "perf"), "/root/lilsmoke-vm-validation/storage/themes/" + THEMES[slug],
                *size.split("x"), str(icon), "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
            if a.mips: command = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(sdk / "target"), *command]
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
            if a.short: env["C41_SHORT"] = "1"
            with (out / (key + ".csv")).open("w") as data, (out / (key + ".log")).open("w") as log:
                subprocess.run(command, stdout=data, stderr=log, env=env, timeout=600, check=True)
            lines = [r for r in (out / (key + ".csv")).read_text().splitlines() if r.count(",") == 9]
            rows = list(csv.DictReader(lines))
            if a.compare:
                old_lines = [r for r in (Path(a.compare) / (key + ".csv")).read_text().splitlines() if r.count(",") == 9]
                old = list(csv.DictReader(old_lines))
                differences = [(r["phase"], r["frame"], r["hash"], b["hash"]) for r, b in zip(rows, old)
                    if any(r[k] != b[k] for k in ("phase", "frame", "ticks", "hash"))]
                assert len(rows) == len(old) and not differences, (key, differences[:12])
            result = {"frames": len(rows), "max_image_bytes": max(int(r["image_bytes"]) for r in rows)}
            for phase in dict.fromkeys(r["phase"] for r in rows):
                values = [r for r in rows if r["phase"] == phase]
                result[phase] = {f: round(statistics.mean(float(r[f]) for r in values), 6)
                    for f in ("render_ms", "compare_ms", "present_ms")}
                times = sorted(float(r["render_ms"]) for r in values)
                result[phase]["p95_render_ms"] = times[min(len(times)-1, int(len(times)*.95))]
            report["results"][key] = result
            (out / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__": main()
