#!/usr/bin/env python3
"""Run the original game through controller input; no engine state shortcuts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
from PIL import Image

HERE = Path(__file__).resolve().parent
BUILD = HERE / "_build"
SDK = Path("/root/lilsmoke-os/build/buildroot/output")


def host_budget():
    resource.setrlimit(resource.RLIMIT_AS, (96 * 1024 * 1024,) * 2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=("host", "mips"), required=True)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=1560)
    args = parser.parse_args()
    assert 600 <= args.frames <= 5000
    rom_hash = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    built = json.loads((BUILD / (args.target + "-build.json")).read_text())
    assert not built["distributable"] and not built["rendererBinaryRedistributionAllowed"]
    binary = Path(built["privateBinary"])
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == built["binarySha256"]
    evidence = BUILD / (args.target + "-private-evidence")
    evidence.mkdir(exist_ok=False)
    prefix = ["qemu-mipsel", "-cpu", "74Kf", "-R", str(64 * 1024 * 1024), "-L", str(SDK / "staging")] if args.target == "mips" else []
    with tempfile.TemporaryDirectory(prefix=args.target + "-private-run-", dir=BUILD) as temporary:
        home = Path(temporary)
        env = dict(os.environ, HOME=str(home), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SM64_ORIGINAL_TEST_FRAMES=str(args.frames), SM64_ORIGINAL_RENDER_EVERY="15",
                   SM64_ORIGINAL_EVIDENCE=str(evidence), OMP_NUM_THREADS="1")
        log = evidence / "engine.log"
        with log.open("w") as output:
            result = subprocess.run([*prefix, str(binary)], cwd=home, env=env, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=600,
                                    preexec_fn=host_budget if args.target == "host" else None)
        text = log.read_text(errors="replace")
        print("\n".join(text.splitlines()[-20:]), flush=True)
        assert result.returncode == 0, result.returncode
        assert "OriginalEngineTest=FINISHED" in text
        assert re.search(r"AudioNonzero=[1-9]\d*", text), "original synthesis must generate nonzero audio"
        assert re.search(r"OriginalFrame=\d+ Level=16 Area=1", text), "original castle grounds must be reached"
        images = []
        for file in sorted(evidence.glob("frame-*.ppm")):
            image = Image.open(file)
            assert image.size == (320, 240)
            colors = image.getcolors(320 * 240)
            assert colors is not None and len(colors) > 4, "blank/single-color original engine frame"
            image.save(file.with_suffix(".png"))
            images.append({"frame": int(file.stem.split("-")[1]), "colors": len(colors),
                           "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
        assert len(images) >= 6
        saves = []
        for file in sorted(home.rglob("*")):
            if file.is_file(): saves.append({"name": str(file.relative_to(home)), "bytes": file.stat().st_size})
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == rom_hash
    report = {"status": "PASS", "target": args.target, "frames": args.frames, "images": images,
              "romUnchanged": True, "isolatedSavesRemoved": True, "temporaryFiles": saves,
              "evidencePrivateNotDistributable": True, "noEngineStateInjection": True,
              "guestAddressSpaceBudgetMiB": 64 if args.target == "mips" else None,
              "hostAddressSpaceBudgetMiB": 96 if args.target == "host" else None}
    (BUILD / (args.target + "-smoke.json")).write_text(json.dumps(report, indent=2) + "\n")
    print("PASS private original title/menu/castle + audio synthesis; original ROM unchanged")


if __name__ == "__main__":
    main()
