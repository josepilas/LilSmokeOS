# SPDX-License-Identifier: MPL-2.0
"""Native-MIPS source/owner-ROM extraction QA, without committing a game cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

from cook import Rom
from native_build import NativeBuild


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    app = args.out / 'Test App'
    app.mkdir()
    (app / 'runtime').symlink_to(args.runtime.resolve(), target_is_directory=True)
    stage = app / 'transaction'
    stage.mkdir()
    before = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    updates = []
    last = None

    def progress(phase, percent, count):
        nonlocal last
        updates.append((phase, percent, count))
        if (phase, percent) != last:
            print(json.dumps(dict(phase=phase, percent=percent, bytes=count)), flush=True)
            last = (phase, percent)

    rom = Rom(args.rom)
    try:
        rom.write_normalized(stage / 'baserom.us.z64', progress)
    finally:
        rom.close()
    builder = NativeBuild(app)

    def extracted_only(stage, work, extracted, report, cancel):
        assert not (stage / 'baserom.us.z64').exists(), 'ROM must be moved, not copied'
        assert (work / 'baserom.us.z64').stat().st_size == 8 * 1024 * 1024
        assets = json.loads((work / 'assets.json').read_text())
        missing = [name for name, value in assets.items()
                   if not name.startswith('@') and 'us' in value[-1]
                   and not (work / name).is_file()]
        assert not missing, missing[:10]
        assert extracted > 0
        return work, extracted

    builder.compile = extracted_only
    work, extracted = builder.build(stage, progress, lambda: False)
    assert any(phase == 'Unpacking engine' and count > 0 for phase, _, count in updates)
    assert any(phase == 'Extracting assets' and count > 0 for phase, _, count in updates)
    assert before == hashlib.sha256(args.rom.read_bytes()).hexdigest()
    result = dict(passed=True, extracted_bytes=extracted, real_owner_rom=True,
                  original_preserved=True, original_game_compiled=False,
                  physical_hc16xx_test=False,
                  native_mips=os.uname().machine.startswith('mips'))
    (args.out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    # Extracted owner assets stay private, and are not release material.
    shutil.rmtree(work)
    print(json.dumps(result, indent=2), flush=True)


if __name__ == '__main__':
    main()
