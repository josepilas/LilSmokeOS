#!/usr/bin/env python3
"""Replay original production-view QA rasters through actual MIPS Renderer code."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from PIL import Image
from test_theme_motion import THEMES


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--before", required=True)
    p.add_argument("--after", required=True)
    p.add_argument("--frames", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--sdk", default="/root/lilsmoke-os/build/buildroot/output")
    a = p.parse_args()
    out = Path(a.output); out.mkdir(parents=True, exist_ok=True)
    sdk = Path(a.sdk)
    cxx = sdk / "host/bin/mipsel-buildroot-linux-musl-g++"
    harness = Path(__file__).with_name("theme-damage-replay.cpp")
    for name, src in (("before", a.before), ("after", a.after)):
        src = Path(src)
        command = [str(cxx), "-std=c++17", "-O3", "-flto", "-Wall", "-Wextra", "-Werror", "-I" + str(src),
            "-I" + str(sdk / "staging/usr/include"), str(harness),
            *[str(src / (n + ".cpp")) for n in ("Renderer", "Font", "Log")],
            "-lSDL2_image", "-lSDL2_ttf", "-lSDL2", "-ldl", "-o", str(out / (name + "-mips"))]
        (out / (name + "-command.json")).write_text(json.dumps(command, indent=2))
        subprocess.run(command, check=True)
    results = {}
    for size in ("856x480", "1280x720"):
        w, h = map(int, size.split("x"))
        paths = []
        for theme in THEMES:
            directory = Path(a.frames) / theme / size
            paths += [directory / "system-before.png"]
            all_frames = sorted(directory.glob("system-*-t*.png"))
            paths += [path for path in all_frames if int(path.stem.rsplit("t", 1)[1]) in (0, 80, 200, 400, 500)]
            paths += [directory / n for n in ("grid-before.png", "grid-t0.png", "grid-t100.png", "grid-t500.png")]
        stream = out / (size + ".argb")
        manifest = []
        # Interleave identical frames to cover steady UI as well as animation.
        with stream.open("wb") as f:
            f.write(struct.pack("<III", w, h, len(paths) * 2))
            for path in paths:
                im = Image.open(path).convert("RGBA")
                assert im.size == (w, h)
                raw = im.tobytes("raw", "BGRA")
                manifest.append({"path": str(path), "sha256_argb": hashlib.sha256(raw).hexdigest()})
                f.write(raw); f.write(raw)
        (out / (size + "-frames.json")).write_text(json.dumps(manifest, indent=2))
        result = {}
        for name in ("before", "after"):
            with (out / (size + "-" + name + ".csv")).open("w") as data:
                subprocess.run(["qemu-mipsel", "-cpu", "74Kf", "-L", str(sdk / "target"),
                    str(out / (name + "-mips")), str(stream)], stdout=data, check=True, timeout=600)
            rows = list(csv.DictReader((out / (size + "-" + name + ".csv")).open()))
            result[name] = {phase: sum(float(r["compare_ms"]) for r in rows if int(r["frame"]) % 2 == parity) / (len(rows) / 2)
                for phase, parity in (("changed_mean_ms", 0), ("identical_mean_ms", 1))}
            result[name]["frames"] = len(rows)
        results[size] = result
        (out / "report.json").write_text(json.dumps({"kind": "QEMU user-mode 74Kf, not hardware FPS", "results": results}, indent=2))
        print(size, result, flush=True)


if __name__ == "__main__":
    main()
