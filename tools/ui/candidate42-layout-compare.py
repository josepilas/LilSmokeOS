#!/usr/bin/env python3
"""Compare preserved C41/C42 deterministic frames, not hardware footage."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops


def nonzero(image):
    bands = image.convert("RGB").split()
    return ImageChops.lighter(ImageChops.lighter(bands[0], bands[1]), bands[2]).point(
        [0] + [255] * 255)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--before", required=True, type=Path)
    p.add_argument("--after", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--keyframes-only", action="store_true")
    a = p.parse_args()
    report = {"kind": "preserved deterministic software frames, not hardware FPS", "frames": 0,
              "identical": 0, "changed": [], "missing": []}
    for before in sorted(a.before.glob("*/*/frames.tsv")):
        relative = before.parent.relative_to(a.before)
        names = [line.split("\t")[0] for line in before.read_text().splitlines()[1:]]
        if a.keyframes_only:
            names = [name for name in names if name.endswith(("-t0000", "-t0100", "-t0250", "-t0500"))]
        for name in names:
            old, new = before.parent / (name + ".png"), a.after / relative / (name + ".png")
            if not new.is_file(): report["missing"].append(str(new)); continue
            with Image.open(old) as old_file, Image.open(new) as new_file:
                old_image, new_image = old_file.convert("RGB"), new_file.convert("RGB")
            if old_image.size != new_image.size: raise RuntimeError("viewport mismatch")
            diff = ImageChops.difference(old_image, new_image)
            report["frames"] += 1
            if diff.getbbox() is None: report["identical"] += 1; continue
            mask = nonzero(diff)
            changed = mask.histogram()[255]
            # Test whether old colors exist within one new pixel. Exclude the
            # outer pixel, where offset wraps; this is a diagnostic, not a pass gate.
            unmatched = mask
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx or dy:
                        neighbor = nonzero(ImageChops.difference(old_image, ImageChops.offset(new_image, dx, dy)))
                        unmatched = ImageChops.darker(unmatched, neighbor)
            inner = unmatched.crop((1, 1, old_image.width - 1, old_image.height - 1))
            report["changed"].append({"path": str(relative / name), "pixels": changed,
                "fraction": changed / (old_image.width * old_image.height), "bbox": diff.getbbox(),
                "pixels_unmatched_within_one_pixel_interior": inner.histogram()[255]})
        print(str(relative), "frames=", report["frames"], flush=True)
    report["max_changed_fraction"] = max((v["fraction"] for v in report["changed"]), default=0)
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("changed", "missing")}, indent=2))
    print("changed_frames=", len(report["changed"]), "missing_frames=", len(report["missing"]))
    if report["missing"]: raise SystemExit(1)


if __name__ == "__main__": main()
