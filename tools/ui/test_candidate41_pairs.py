#!/usr/bin/env python3
"""Interleaved, already-built production workloads; never hardware FPS."""
import argparse
import csv
import json
import os
from pathlib import Path
import statistics
import subprocess
from test_theme_motion import THEMES


def rows(path):
    return list(csv.DictReader(line for line in path.read_text().splitlines() if line.count(",") == 9))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--before", required=True, type=Path)
    p.add_argument("--after", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--repeat", type=int, default=2)
    p.add_argument("--themes", default=",".join(THEMES))
    p.add_argument("--sizes", default="856x480,1280x720")
    p.add_argument("--host", action="store_true")
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    report = {"not_hardware_fps": True, "timing_clock": "thread CPU" if "C41_CPU_CLOCK" in os.environ else "wall",
              "asset_cache_mb": int(os.environ.get("C41_CACHE_MB", "8")),
              "interleaved_order": "AB then BA", "results": {}}
    for size in a.sizes.split(","):
        for slug in a.themes.split(","):
            key = slug + "-" + size
            runs = {"before": [], "after": []}
            for repeat in range(a.repeat):
                for label in (("before", "after") if repeat % 2 == 0 else ("after", "before")):
                    directory = getattr(a, label)
                    command = [str(directory / "perf"), "/root/lilsmoke-vm-validation/storage/themes/" + THEMES[slug],
                               *size.split("x"), str(directory / "icon.png"), "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]
                    if not a.host:
                        command = ["qemu-mipsel", "-cpu", "74Kf", "-L", "/root/lilsmoke-os/build/buildroot/output/target", *command]
                    name = key + "-" + label + "-" + str(repeat)
                    print(name, flush=True)
                    env = dict(os.environ, C41_SHORT="1", LILSMOKE_UI_FAST_BLITS="1")
                    with (a.output / (name + ".csv")).open("w") as data, (a.output / (name + ".log")).open("w") as log:
                        subprocess.run(command, stdout=data, stderr=log, check=True, timeout=600, env=env)
                    runs[label].extend(rows(a.output / (name + ".csv")))
                before, after = (runs[label][-113:] for label in ("before", "after"))
                assert len(before) == len(after) == 113, key
                assert [(r["phase"], r["frame"], r["ticks"], r["hash"]) for r in before] == [
                    (r["phase"], r["frame"], r["ticks"], r["hash"]) for r in after], key
            result = {"pixel_identical_frames": len(runs["after"])}
            for label, values in runs.items():
                result[label] = {"max_image_bytes": max(int(r["image_bytes"]) for r in values), "phases": {}}
                for phase in dict.fromkeys(r["phase"] for r in values):
                    frames = [r for r in values if r["phase"] == phase]
                    result[label]["phases"][phase] = {
                        field: round(statistics.median(float(r[field]) for r in frames), 6)
                        for field in ("render_ms", "compare_ms", "present_ms")}
                    times = sorted(float(r["render_ms"]) for r in frames)
                    result[label]["phases"][phase]["p95_render_ms"] = times[min(len(times)-1, int(len(times)*.95))]
            report["results"][key] = result
            (a.output / "report.json").write_text(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
