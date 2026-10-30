#!/usr/bin/env python3
"""Pin/audit upstream engine and identify why the private binary cannot ship."""
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
BUILD = HERE / "_build"
PORT = Path("/root/lilsmoke-sm64-soft")
PIN = "a173bd0a3eeed9b064a234e528f128424d46ea08"
ENGINE = ("src/game/", "src/engine/", "src/menu/", "src/goddard/", "src/audio/", "src/buffers/", "levels/", "actors/", "data/")


def main():
    assert subprocess.check_output(["git", "-C", str(PORT), "rev-parse", "HEAD"], text=True).strip() == PIN
    files = subprocess.check_output(["git", "-C", str(PORT), "ls-tree", "-r", "--name-only", PIN], text=True).splitlines()
    copied = Path(json.loads((BUILD / "private-workspace.json").read_text())["source"])
    assert copied.parent in (Path("/tmp"), Path("/root")) and copied.name.startswith("lilsmoke-sm64-original-private-")
    hashes = {}
    for name in files:
        if name.startswith(ENGINE):
            original = (PORT / name).read_bytes()
            assert (copied / name).read_bytes() == original, name
            hashes[name] = hashlib.sha256(original).hexdigest()
    license_file = PORT / "src/pc/gfx/LICENSE.txt"
    assert "Redistributions in binary form are not allowed." in license_file.read_text()
    report = {"portPin": PIN, "engineFilesUnmodified": len(hashes), "engineFileSha256": hashes,
              "graphicsLicenseSha256": hashlib.sha256(license_file.read_bytes()).hexdigest(),
              "graphicsBinaryRedistributionAllowed": False,
              "assetFreeRuntimeImplemented": False,
              "compiledAssetSources": ["generated texture .inc.c includes", "assets/mario_anim_data.c",
                                       "assets/demo_data.c", "sound/sound_data.o", "level/actor vertex/model arrays"],
              "rawSegmentCacheNotDropIn": "NO_SEGMENTED_MEMORY uses relocated native pointers; runtime loading needs cooked data relocation/type/ABI checks, not just raw segment copies"}
    (BUILD / "source-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS", len(hashes), "engine source files byte-identical; renderer/asset-bearing binary not distributable")


if __name__ == "__main__":
    main()
