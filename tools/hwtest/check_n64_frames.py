#!/usr/bin/env python3
"""Verify real MIPS core output and controller-dependent software RDP colors."""
from pathlib import Path
from collections import Counter
import json
from PIL import Image

root = Path(__file__).resolve().parents[2]
out = root / "test-results/n64"
frames = out / "frames"
checks = []
def check(ok, label):
    assert ok, label
    checks.append(label)
    print("PASS", label)
for frame, color, label in [(5, "red", "neutral"), (20, "green", "start"),
                             (35, "blue", "a-button"), (45, "red", "released")]:
    path = frames / f"{frame:04}.raw"
    width, height = map(int, Path(str(path)+".size").read_text().split())
    data = path.read_bytes()
    check(len(data) == width * height * 4, label + " frame has complete XRGB8888 pixels")
    pixels = [data[i:i+4] for i in range(0, len(data), 4)]
    counts = Counter(pixels)
    most, count = counts.most_common(1)[0]
    b, g, r, _ = most
    expected = {"red": r > 200 and g < 30 and b < 30,
                "green": g > 200 and r < 30 and b < 30,
                "blue": b > 200 and r < 30 and g < 30}[color]
    check(expected and count > width * height * 0.9,
          label + " renders expected controller-dependent RDP fill color")
    Image.frombytes("RGB", (width, height), data, "raw", "BGRX").save(out / (label+".png"))
check(len(list(frames.glob("*.raw"))) == 50, "50 actual non-duplicated software frames captured")
(out / "frame-checks.json").write_text(json.dumps({"passed":len(checks),"checks":checks},indent=2)+"\n")
print("N64_FRAME_CHECKS_OK checks="+str(len(checks)))
