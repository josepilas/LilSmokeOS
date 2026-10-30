# SPDX-License-Identifier: MPL-2.0
"""Fresh owner-ROM preparation, full native build, render preflight and cache QA."""
import argparse
import json
from pathlib import Path
import shutil

from cook import Cache, US_SHA1, digest_file
from native_build import NativeBuild


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    app = args.out / 'Test App'
    (app / 'original').mkdir(parents=True)
    (app / 'runtime').symlink_to(args.runtime.resolve(), target_is_directory=True)
    owner_hash = digest_file(args.rom)
    original = app / 'original/Arbitrary owner filename.z64'
    shutil.copyfile(args.rom, original)
    builder = NativeBuild(app)
    cache = Cache(app, builder.identity)
    assert cache.current() is None, 'Test must not reuse an existing cooked game'
    last = None

    def progress(phase, percent, count):
        nonlocal last
        if (phase, percent) != last:
            print(json.dumps(dict(phase=phase, percent=percent, bytes=count)), flush=True)
            last = (phase, percent)

    game = cache.prepare(builder.build, progress, resumable=True)
    assert game == cache.current()
    assert not original.exists(), 'Only the committed test original should be deleted'
    assert owner_hash == digest_file(args.rom), 'Owner Downloads ROM must stay intact'
    record = json.loads((cache.root / 'current.json').read_text())
    assert record['rom_sha1'] == US_SHA1 and record['extracted_bytes'] > 0
    assert 'OriginalEngineTest=FINISHED Frames=30 Rendered=30' in (game.parent / 'validation.log').read_text()

    def unexpected_builder(*_):
        raise AssertionError('Second launch must reuse the validated cache')

    assert cache.prepare(unexpected_builder, progress, resumable=True) == game
    report = dict(passed=True, full_native_mips_build=True, fresh_cache=True,
                  rendered_preflight_frames=30, extracted_bytes=record['extracted_bytes'],
                  game_sha256=digest_file(game), repeat_launch=True,
                  deletes_only_committed_app_original=True, owner_rom_preserved=True,
                  physical_hc16xx=False)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
