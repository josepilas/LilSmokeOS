#!/usr/bin/env python3
"""Private Mario pose evidence; ROM/pack bytes never retained or distributed."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

B = Path(__file__).resolve().parent / "_build/mario-worker"
SDK = Path("/root/lilsmoke-os/build/buildroot/output")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--evidence-name", default="private-evidence")
    args = parser.parse_args()
    assert re.fullmatch(r"private-evidence[-a-zA-Z0-9]*", args.evidence_name), "evidence stays in worker build directory"
    before = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    evidence = B / args.evidence_name
    evidence.mkdir(exist_ok=True)
    cases = (("idle", 0), ("walk", 19), ("walk", 38), ("jump", 6))
    results = []
    with tempfile.TemporaryDirectory(prefix="mario-render-") as temporary:
        pack = Path(temporary) / "mario.pdat"
        subprocess.run([str(B / "host-private"), str(args.rom), str(pack)], check=True)
        for target in ("host", "mips"):
            prefix = ["qemu-mipsel", "-cpu", "74Kf", "-L", str(SDK / "staging")] if target == "mips" else []
            images = []
            for motion, frame in cases:
                image = evidence / f"{target}-{motion}-{frame}.ppm"
                if image.exists():
                    raise RuntimeError(f"existing evidence preserved; choose a new evidence directory: {image}")
                run = subprocess.run([*prefix, str(B / (target + "-preview")), str(pack), motion,
                                      str(frame), str(image)], check=True, text=True, capture_output=True)
                assert not run.stderr, run.stderr
                data = image.read_bytes()
                assert data.startswith(b"P6\n320 240\n255\n") and len(data) == 230415
                # Lossless container conversion only, for native QA image display.
                from PIL import Image
                Image.open(image).save(image.with_suffix(".png"))
                digest = hashlib.sha256(data).hexdigest()
                images.append(digest)
                results.append({"target": target, "motion": motion, "frame": frame,
                                "imageSha256": digest, "output": run.stdout.strip()})
                print(target, run.stdout.strip(), "ImageSHA256=" + digest, flush=True)
            assert len(set(images)) == len(cases), "animation poses must render distinct genuine images"
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == before, "ROM modified"
    report = {"romUnchanged": True, "temporaryPackRemoved": True, "privateImagesOnly": True,
              "evidenceDirectory": args.evidence_name,
              "width": 320, "height": 240, "results": results}
    (B / "mario-render-results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS host/MIPS actual Mario private poses; ROM unchanged; temporary pack removed")


if __name__ == "__main__":
    main()
