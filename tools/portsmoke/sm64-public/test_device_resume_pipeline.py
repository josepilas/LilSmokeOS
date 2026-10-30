#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify the owner's legacy work against fresh extraction, then resume/build.

All inputs and outputs are private QA data, never package or release inputs.
Run with the bundled native MIPS Python, not a host game executable.
"""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import time

import cook
from cook import Cache, Cancelled, Rom, atomic_json, digest_file
from native_build import NativeBuild, copy_buffered, prepare_engine_makefile
from build_progress import classify_command
from resume import MARKER, checkpoint, inflight, source_files, validate


def import_private(archive, out):
    out.mkdir()
    total = 0
    with tarfile.open(archive, 'r:') as source:
        for item in source:
            name = PurePosixPath(item.name)
            if (name.is_absolute() or '..' in name.parts or '\\' in item.name or
                    not (item.isdir() or item.isfile())):
                raise ValueError('Unsafe private backup entry')
            if not name.parts:
                continue
            target = out.joinpath(*name.parts)
            if item.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                total += item.size
                if total > 256 * 1024 * 1024:
                    raise ValueError('Private backup exceeds QA budget')
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(item) as incoming, target.open('xb') as output:
                    shutil.copyfileobj(incoming, output, 128 * 1024)
                target.chmod(item.mode & 0o777)
            source.members.clear()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--legacy-tar', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--future-inputs', action='store_true')
    parser.add_argument('--snapshot', type=Path,
        help='Retain private completed build inputs before cache pruning')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py'):
        assert digest_file(here / name) == digest_file(args.runtime / name), 'QA runtime source differs: ' + name
    args.out.mkdir(parents=True, exist_ok=False)
    app = args.out / 'Test App'
    (app / 'original').mkdir(parents=True)
    (app / 'runtime').symlink_to(args.runtime.resolve(), target_is_directory=True)
    original = app / 'original/Owner ROM.z64'
    owner_hash = digest_file(args.rom)
    copy_buffered(args.rom, original)
    last = None
    last_time = 0
    texture_seen = False
    compile_seen = False

    def progress(phase, percent, count):
        nonlocal last, last_time, texture_seen, compile_seen
        texture_seen |= phase.startswith('Textures ')
        compile_seen |= phase.startswith('Compiling ')
        now = time.monotonic()
        if (phase.split(' ')[0], percent) != last or now - last_time > 10:
            print(json.dumps(dict(phase=phase, percent=percent, bytes=count)), flush=True)
            last, last_time = (phase.split(' ')[0], percent), now

    reference = args.out / 'reference'
    reference.mkdir()
    rom = Rom(original)
    try:
        rom.write_normalized(reference / 'baserom.us.z64', progress)
    finally:
        rom.close()
    builder = NativeBuild(app)
    builder.compile = lambda stage, work, count, callback, cancel: (stage, count)
    builder.build(reference, progress, lambda: False)
    expected = json.loads((reference / MARKER).read_text())

    cache = Cache(app, builder.identity)
    legacy = cache.root / 'working-imported'
    import_private(args.legacy_tar, legacy)
    prepare_engine_makefile(legacy / 'source', fast_bin2c=True)
    actual = {name: dict(bytes=size, sha256=digest_file(path))
              for name, path, size in source_files(legacy / 'source')}
    changed = {name for name in actual.keys() | expected['files'].keys()
               if actual.get(name) != expected['files'].get(name)}
    if changed:
        print(json.dumps(dict(legacy_differences=sorted(changed))), flush=True)
        raise ValueError('Legacy work differs from fresh trusted extraction')
    # Windows FAT backups do not retain Unix executable bits. Restore only
    # modes of the now-byte-verified trusted reference, never arbitrary inputs.
    for name in actual:
        (legacy / 'source' / name).chmod((reference / 'source' / name).stat().st_mode & 0o777)
        if args.future_inputs:
            os.utime(legacy / 'source' / name, (1893456000, 1893456000))
    atomic_json(legacy / MARKER, expected)
    validate(legacy, builder.identity, progress)
    # Regenerate the final potentially interrupted texture producer from the
    # device log, even if its file happened to be complete at cancellation.
    previous = None
    for line in (legacy / 'build.log').read_text(errors='replace').splitlines():
        task = classify_command(line)
        if task and task.target:
            previous = task
    if previous is not None:
        inflight(legacy, previous.target)
    atomic_json(cache.root / 'active.json', dict(schema=1, recipe=builder.identity,
        rom_sha1=cook.US_SHA1, generation=legacy.name))
    (args.out / 'legacy-verification.json').write_text(json.dumps(dict(
        passed=True, reference_source_files=len(actual),
        extracted_bytes=expected['extracted_bytes'], recipe=builder.identity,
        source_verified_against_fresh_rom_extraction=True), indent=2) + '\n')
    stop = False

    def interrupt(phase, percent, count):
        nonlocal stop
        progress(phase, percent, count)
        if phase.startswith('Textures ') and int(phase.split()[1].split('/')[0]) >= 3:
            stop = True

    try:
        Cache(app, builder.identity).prepare(NativeBuild(app).build, interrupt,
            lambda: stop, resumable=True)
        raise AssertionError('The cancellation fixture did not interrupt texture work')
    except Cancelled:
        pass
    assert original.exists() and Cache(app, builder.identity).current() is None
    assert (legacy / MARKER).is_file() and (cache.root / 'active.json').is_file()
    print('QA_RESUME_CANCEL_PASSED', flush=True)
    cache = Cache(app, builder.identity)
    def complete_build(stage, callback, cancel):
        result = NativeBuild(app).build(stage, callback, cancel)
        if args.snapshot is not None:
            args.snapshot.mkdir(parents=True, exist_ok=False)
            target = args.snapshot / 'PRIVATE-stage'
            shutil.copytree(stage, target)
            binary = target / 'source/build/original-mips/sm64.us.f3dex2e'
            atomic_json(args.snapshot / 'capture.json', dict(captured=True,
                native=str(args.out.resolve()), stage=str(target.resolve()),
                generated_elf_sha256=digest_file(binary),
                captured_after_build_before_cache_prune=True,
                private_game_data=True, requires_native_commit_hash_match=True))
        return result

    game = cache.prepare(complete_build, progress, resumable=True)
    assert game == cache.current() and not original.exists()
    assert owner_hash == digest_file(args.rom)
    assert 'OriginalEngineTest=FINISHED Frames=30 Rendered=30' in (game.parent / 'validation.log').read_text()
    assert not (game.parent / 'source').exists()
    def unexpected(*_):
        raise AssertionError('A committed game must not rebuild')
    assert cache.prepare(unexpected, progress, resumable=True) == game
    report = dict(passed=True, full_native_mips_build=True, legacy_source_verified=True,
        cancellation_preserves_original=True, resumes_without_reextracting=True,
        texture_progress=texture_seen, compilation_progress=compile_seen,
        extracted_bytes=expected['extracted_bytes'], rendered_preflight_frames=30,
        repeat_launch=True, deletes_only_committed_app_original=True,
        owner_rom_preserved=True, game_sha256=digest_file(game), physical_hc16xx=False)
    report['source_dates_in_future'] = args.future_inputs
    report['runtime_sources'] = {name: digest_file(args.runtime / name)
        for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py')}
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
