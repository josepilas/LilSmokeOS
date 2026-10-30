#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Recompile/relink only the renderer against read-only public-build objects.

No source/runtime/asset snapshot is copied. Resulting games are private QA,
never public package inputs. All compiler processes run serially on CPU 3.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import tarfile
import time

from make_occlusion_patch import transform
from test_span_div import SDK, budget


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def instrument(text):
    text = text.replace('static inline bool opt_span_visible(', '''static unsigned long long block_counts[17], hidden_counts[17], untested_counts[17];
__attribute__((destructor)) static void report_occlusion(void) {
    for (int i = 1; i <= 16; ++i)
        fprintf(stderr, "OCCLUSION count=%d blocks=%llu hidden=%llu untested=%llu\\n",
            i, block_counts[i], hidden_counts[i], untested_counts[i]);
}
static inline bool opt_span_visible(''', 1)
    text = text.replace('    if (!z_test) return true;\n',
        '    ++block_counts[count];\n    if (!z_test) { ++untested_counts[count]; return true; }\n', 1)
    text = text.replace('    return false;\n}\n\n/* --- opaque textured',
        '    ++hidden_counts[count];\n    return false;\n}\n\n/* --- opaque textured', 1)
    assert '++hidden_counts[count]' in text
    return text


def build(snapshot, out, runtime):
    source = snapshot / 'PRIVATE-stage/source'
    text = (source / 'src/pc/gfx/gfx_soft.c').read_text()
    with tarfile.open(runtime / 'source.tar.gz') as archive:
        if archive.extractfile('src/pc/gfx/gfx_soft.c').read() != text.encode():
            raise ValueError('Snapshot renderer differs from public operational source')
    commands = (snapshot / 'PRIVATE-stage/build.log').read_text().replace('\\\n', ' ').splitlines()
    compile_command = next(shlex.split(line) for line in commands
        if ' -c ' in line and ' -o build/original-mips/src/pc/gfx/gfx_soft.o ' in line)
    link_command = next(shlex.split(line) for line in commands if ' -nostdlib ' in line and ' -o build/original-mips/sm64.us.f3dex2e ' in line)
    compiler = SDK / 'host/bin/mipsel-buildroot-linux-musl-gcc'
    platform_command = [arg.replace('src/pc/gfx/gfx_soft.c', 'lilsmoke/platform_sdl2.c')
        .replace('src/pc/gfx/gfx_soft.o', 'lilsmoke-platform.o') for arg in compile_command
        if arg not in ('-O3', '-fno-math-errno', '-mno-shared', '-mplt')]
    platform = (source / 'lilsmoke/platform_sdl2.c').read_text()
    guard = '    if (preflight && limit != 30) {'
    if platform.count(guard) != 1:
        raise ValueError('Unknown platform preflight guard')
    # The shipped SDL is DirectFB-only. Extend its existing surface-only QA
    # mode to the long scripted test, without changing gameplay/render/audio.
    headless = out / 'PRIVATE-headless-platform.c'
    headless.write_text(platform.replace(guard, '    if (preflight && !scripted) {', 1))
    headless_obj = out / 'PRIVATE-headless-platform.o'
    argv = [str(compiler)] + platform_command[1:]
    argv[argv.index('-o') + 1] = str(headless_obj)
    argv[argv.index('lilsmoke/platform_sdl2.c')] = str(headless)
    for flag in ('-MF', '-MT'):
        if flag in argv:
            i = argv.index(flag)
            del argv[i:i+2]
    argv = [a for a in argv if a not in ('-MMD', '-MP')]
    argv += ['-Ililsmoke']
    with (out / 'headless-compile.log').open('w') as log:
        subprocess.run(argv, cwd=source, stdout=log, stderr=subprocess.STDOUT,
            preexec_fn=budget, check=True, timeout=600)
    products = {}
    for version, renderer in (('before', text), ('after', transform(text)), ('profile', instrument(transform(text)))):
        file = out / ('PRIVATE-gfx-soft-' + version + '.c')
        file.write_text(renderer)
        obj = out / ('PRIVATE-gfx-soft-' + version + '.o')
        command = [str(compiler)]
        skip = False
        for arg in compile_command[1:]:
            if skip:
                skip = False
                continue
            if arg in ('-MF', '-MT', '-o'):
                skip = True
                continue
            if arg in ('-MMD', '-MP', '-c', 'src/pc/gfx/gfx_soft.c'):
                continue
            command.append(arg)
        command += ['-I' + str(source / 'src/pc/gfx'), '-c', str(file), '-o', str(obj)]
        game = out / ('PRIVATE-game-' + version)
        link = [str(compiler) if i == 0 else str(game) if i and link_command[i-1] == '-o'
                else str(obj) if arg == 'build/original-mips/src/pc/gfx/gfx_soft.o' else arg
                for i, arg in enumerate(link_command)]
        # The public preparation copies these authored input objects from its
        # runtime. A pruned snapshot may omit one; use that same reviewed input.
        for i, arg in enumerate(link):
            if arg.startswith('build/original-mips/lilsmoke-') and arg.endswith('.o') and not (source / arg).is_file():
                replacement = runtime / 'objects' / Path(arg).name
                if not replacement.is_file():
                    raise ValueError('Missing original link input: ' + arg)
                link[i] = str(replacement)
        for kind, argv in (('compile', command), ('link', link)):
            if kind == 'link':
                production_link = list(argv)
                argv = [str(headless_obj) if a == 'build/original-mips/lilsmoke-platform.o' else a for a in argv]
            (out / f'{version}-{kind}-command.json').write_text(json.dumps(argv, indent=2) + '\n')
            with (out / f'{version}-{kind}.log').open('w') as log:
                subprocess.run(argv, cwd=source, stdout=log, stderr=subprocess.STDOUT,
                    preexec_fn=budget, check=True, timeout=600)
        if version == 'after':
            production_game = out / 'PRIVATE-game-production'
            production_link[production_link.index('-o')+1] = str(production_game)
            with (out / 'production-link.log').open('w') as log:
                subprocess.run(production_link, cwd=source, stdout=log, stderr=subprocess.STDOUT,
                    preexec_fn=budget, check=True, timeout=600)
            products['production'] = dict(binary=production_game.name, sha256=sha(production_game),
                platform_unchanged=True, renderer_object_sha256=sha(obj))
        products[version] = dict(binary=game.name, sha256=sha(game), renderer_sha256=sha(file), object_sha256=sha(obj))
    products.update(source=str(source), reference_source_sha256=hashlib.sha256(text.encode()).hexdigest(),
        cached_game_sha256=sha(source / 'build/original-mips/sm64.us.f3dex2e'),
        compiler=subprocess.check_output([str(compiler), '--version'], text=True).splitlines()[0],
        headless_platform_only_delta='allow existing surface-only QA mode beyond 30 frames',
        no_original_source_or_objects_modified=True)
    (out / 'build.json').write_text(json.dumps(products, indent=2) + '\n')
    return products


def execute(out, products, runtime, name, version, options, frames):
    folder = out / name
    folder.mkdir(exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SM64_', 'LILSMOKE_', 'QEMU_'))}
    env.update(HOME=str(folder), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
        SM64_ORIGINAL_TEST_FRAMES=str(frames), SM64_ORIGINAL_RENDER_EVERY='1',
        SM64_ORIGINAL_EVIDENCE=str(folder), SM64_ORIGINAL_PREFLIGHT='1', OMP_NUM_THREADS='1', **options)
    binary = out / products[version]['binary']
    assert sha(binary) == products[version]['sha256']
    command = ['qemu-mipsel', '-cpu', '74Kf', '-R', '100663296', '-L', str(SDK / 'staging'),
        '-E', 'LD_LIBRARY_PATH=' + str(runtime / 'lib'), str(binary)]
    start = time.monotonic()
    with (folder / 'engine.log').open('w') as log:
        subprocess.run(command, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT,
            preexec_fn=budget, timeout=600, check=True)
    log = (folder / 'engine.log').read_text()
    assert f'Frames={frames} Rendered={frames}' in log and 'Level=16 Area=1' in log
    audio = re.search(r'AudioBlocks=(\d+) AudioNonzero=(\d+) AudioHash=(\w+)', log).groups()
    assert int(audio[1]) > 0
    profiles = [(int(n), float(w), float(r)) for n, w, r in re.findall(
        r'SM64_PROFILE frames=(\d+) fps=[\d.]+ work_ms=([\d.]+) raster_ms=([\d.]+)', log)]
    assert sum(n for n, _, _ in profiles) == frames, profiles
    result = dict(name=name, version=version, frames=frames, state=re.findall(r'^OriginalFrame=.*$', log, re.M),
        audio=audio, images={p.name: sha(p) for p in folder.glob('*.ppm')},
        raster_ms=sum(n*r for n, _, r in profiles)/frames,
        work_ms=sum(n*w for n, w, _ in profiles)/frames,
        elapsed_seconds=time.monotonic()-start)
    assert len(result['images']) >= 8
    # Keep only hashes of private captures, not another set of game assets.
    for ppm in folder.glob('*.ppm'):
        ppm.unlink()
    print(name, round(result['raster_ms'], 3), 'raster ms (QEMU only)', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--reuse-build', action='store_true')
    parser.add_argument('--frames', type=int, default=2700)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.reuse_build:
        products = json.loads((out / 'build.json').read_text())
    else:
        out.mkdir(exist_ok=False, parents=True)
        products = build(args.snapshot.resolve(), out, args.runtime.resolve())
    if args.build_only:
        return
    results = []
    for sample in range(3):
        for version in (('before', 'after') if sample % 2 == 0 else ('after', 'before')):
            results.append(execute(out, products, args.runtime, f'256-{sample}-{version}', version,
                {'SM64_SOFTWARE_WIDTH': '256'}, args.frames))
    for name, options in (('320', {'SM64_SOFTWARE_WIDTH': '320'}),
            ('filter-smooth', {'SM64_SOFTWARE_WIDTH': '256', 'SM64_WORLD_FILTER': '1', 'SM64_WORLD_SCALE': 'smooth'}),
            ('cycle', {'SM64_ORIGINAL_TEST_WIDTH_CYCLE': '1'})):
        for version in ('before', 'after'):
            results.append(execute(out, products, args.runtime, name + '-' + version, version, options, args.frames))
    for before in (r for r in results if r['version'] == 'before'):
        after = next(r for r in results if r['name'] == before['name'].replace('before', 'after'))
        for field in ('state', 'audio', 'images'):
            assert before[field] == after[field], (before['name'], field)
    profiled = execute(out, products, args.runtime, 'profile', 'profile', {'SM64_SOFTWARE_WIDTH': '256'}, args.frames)
    assert profiled['state'] == results[0]['state'] and profiled['images'] == results[0]['images']
    histogram = [dict(zip(('count', 'blocks', 'hidden', 'untested'), map(int, row))) for row in re.findall(
        r'OCCLUSION count=(\d+) blocks=(\d+) hidden=(\d+) untested=(\d+)', (out / 'profile/engine.log').read_text())]
    assert len(histogram) == 16 and sum(r['hidden'] for r in histogram) > 0
    report = dict(passed=True, actual_public_original_engine=True, frames_per_run=args.frames,
        total_rendered_frames=sum(r['frames'] for r in results)+profiled['frames'],
        pixel_state_audio_equal=True, cpu=3, nice=10, compiler_as_mib=768, qemu_guest_va_mib=96,
        physical_fps_measured=False, whole_system_vm=False, surface_only_qa_adapter=True,
        production_platform_unchanged=True, histogram=histogram, runs=results)
    report['median_256_raster_ms'] = {v: statistics.median(r['raster_ms'] for r in results
        if r['version'] == v and r['name'].startswith('256-')) for v in ('before', 'after')}
    report['raster_after_over_before'] = report['median_256_raster_ms']['after']/report['median_256_raster_ms']['before']
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS engine exact output; QEMU raster ratio', report['raster_after_over_before'])


if __name__ == '__main__':
    main()
