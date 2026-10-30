#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Create a lean LOCAL development package, with a separate source supplement.

Only manifest-listed public package inputs are used. Private game/cache/ROM
paths cannot enter the .lilpck. No install, card writes, downloads or publishing.
The unchanged complete base source offer is reused rather than duplicated.
"""
import argparse
import copy
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tarfile
import zipfile

from prepare_release import copy_member, json_bytes, safe_member
from split_sources import verified_members
from cook import Cache, verify_mips_executable

HERE = Path(__file__).resolve().parent
ORIGINAL = HERE.parent / 'sm64-original'
VERSION = '0.0.3-perf.2'
RENDERER = 'src/pc/gfx/gfx_soft.c'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def performance_recipe(original, source_sha256):
    # The bundled backend treats this version as a build-protocol identifier,
    # not the application's display version. Keep its accepted contract.
    if original.get('schema') != 1 or original.get('version') != 'Pre-Alpha-0.0.1':
        raise ValueError('Unsupported base build protocol')
    recipe = copy.deepcopy(original)
    recipe.update(source_sha256=source_sha256, package_version=VERSION,
                  performance_adapter='exact-block-occlusion-v1',
                  candidate_requires_end_to_end_acceptance=True)
    return recipe


def transform(text):
    spec = importlib.util.spec_from_file_location('occlusion_adapter', ORIGINAL / 'make_occlusion_patch.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.transform(text)


def rewrite_source(reader, output, before_sha256, after_sha256):
    seen = set()
    changed = []
    with tarfile.open(fileobj=reader, mode='r:gz') as source, output.open('xb') as raw:
        with gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0, compresslevel=6) as compressed:
            with tarfile.open(fileobj=compressed, mode='w') as target:
                for item in source:
                    safe_member(item.name)
                    if not item.isfile() or item.name in seen:
                        raise ValueError('Unexpected source archive entry')
                    seen.add(item.name)
                    data = source.extractfile(item).read()
                    if item.name == RENDERER:
                        if hashlib.sha256(data).hexdigest() != before_sha256:
                            raise ValueError('Tested public renderer is not this package renderer')
                        data = transform(data.decode()).encode()
                        if hashlib.sha256(data).hexdigest() != after_sha256:
                            raise ValueError('Package transformation differs from tested renderer')
                        item = copy.copy(item)
                        item.size = len(data)
                        changed.append(item.name)
                    target.addfile(item, io.BytesIO(data))
    if changed != [RENDERER]:
        raise ValueError('Expected exactly one changed renderer source')
    return changed


def source_supplement(output, base_sources, delivery, renderer, reports):
    if sha(base_sources) != delivery['sha256']:
        raise ValueError('Complete base source offer hash mismatch')
    with zipfile.ZipFile(base_sources) as base:
        original = base.read('project-source/engine/' + RENDERER).decode()
        if transform(original).encode() != renderer:
            raise ValueError('Base source offer does not match renderer delta')
    files = {'project-source/engine/' + RENDERER: renderer}
    for name in ('make_occlusion_patch.py', 'make_span_div_patch.py', 'span_div_benchmark.c',
                 'endpoint_benchmark.c', 'test_span_div.py', 'test_performance_engine.py',
                 'verify_occlusion_qa.py', 'profile_span_workload.py', 'test_occlusion_adapter.py',
                 '0009-block-occlusion.patch'):
        files['performance-tools/' + name] = (ORIGINAL / name).read_bytes()
    files['performance-tools/prepare_performance_update.py'] = Path(__file__).read_bytes()
    files['performance-tools/test_performance_update.py'] = (HERE / 'test_performance_update.py').read_bytes()
    document = HERE.parents[2] / 'docs/sm64-performance-20261026.md'
    if document.is_file():
        files['performance-tools/sm64-performance-20261026.md'] = document.read_bytes()
    for name in ('stage_payload.py',):
        files['runtime/corresponding-source/build-recipes/' + name] = (HERE / name).read_bytes()
    files['runtime/corresponding-source/build-recipes/private_build.py'] = (ORIGINAL / 'private_build.py').read_bytes()
    for label, report in reports.items():
        files['evidence/' + label + '.json'] = json_bytes(report)
    files['SOURCE-SUPPLEMENT.json'] = json_bytes(dict(schema=1, version=VERSION,
        complete_base_source_offer=delivery, complete_base_source_local_path=str(base_sources.resolve()),
        overlay_files={name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
        only_engine_delta=RENDERER, standalone_complete_source=False,
        rom_included=False, cooked_game_included=False, uploaded=False))
    files['README.txt'] = (
        'LOCAL development source supplement, not an installation dependency.\n'
        'Use the exact complete Pre-Alpha-0.0.2 corresponding-source.zip identified\n'
        'in SOURCE-SUPPLEMENT.json, then overlay this supplement. Dependencies,\n'
        'compiler sources, notices and unchanged project files stay in that base.\n'
        'The sole engine change is project-source/engine/src/pc/gfx/gfx_soft.c.\n'
        'The source-only graphics license and all other retained terms still apply.\n'
        'performance-tools contains the generator, adapters and reproducible tests.\n'
        'No game/renderer executable, ROM or extracted asset is included.\n').encode()
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for name, data in sorted(files.items()):
            target.writestr(name, data)
    return dict(asset=output.name, sha256=sha(output), bytes=output.stat().st_size,
        local_only=True, base_source_offer=delivery, standalone_complete_source=False)


def verify_prepared_game(game, runtime, folder, recipe_data, old_recipe_sha, backend_files):
    """Real production adapter preflight plus the ordinary cache readback path."""
    verify_mips_executable(game)
    folder.mkdir()
    (folder / 'runtime').mkdir()
    for name, data in backend_files.items():
        (folder / 'runtime' / name).write_bytes(data)
    (folder / 'runtime/recipe.json').write_bytes(recipe_data)
    recipe_sha = sha(folder / 'runtime/recipe.json')
    cache = Cache(folder, recipe_sha)
    ready = cache.root / 'ready-performance-20261026'
    ready.mkdir()
    private_copy = ready / 'game'
    shutil.copyfile(game, private_copy)
    private_copy.chmod(0o700)
    marker = dict(schema=1, generation=ready.name, recipe=recipe_sha,
        game_sha256=sha(game), rom_sha1='9bef1128717f958171a4afac3ed78ee2bb4e86ce', extracted_bytes=10679837)
    (cache.root / 'current.json').write_bytes(json_bytes(marker))
    assert cache.current() == private_copy
    assert Cache(folder, old_recipe_sha).current() is None
    # Exercise the shipped entrypoint too, not only Cache.current().
    checked = subprocess.run([sys.executable, '-B', str(folder / 'runtime/native_build.py'),
        '--app', str(folder), '--cached-only'], capture_output=True, text=True, timeout=60)
    (folder / 'cached-backend.log').write_text(checked.stdout + checked.stderr)
    if checked.returncode or json.loads(checked.stdout)['ready'] != str(private_copy):
        raise ValueError('Bundled backend rejected the prepared cache: ' + checked.stdout + checked.stderr)
    sdk = Path('/root/lilsmoke-os/build/buildroot/output')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SM64_', 'LILSMOKE_', 'QEMU_'))}
    env.update(HOME=str(folder.resolve()), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
        SM64_ORIGINAL_PREFLIGHT='1', SM64_ORIGINAL_TEST_FRAMES='30', SM64_ORIGINAL_RENDER_EVERY='1')
    command = ['taskset', '-c', '3', 'nice', '-n', '10', 'qemu-mipsel', '-cpu', '74Kf', '-R', '100663296',
        '-L', str(sdk / 'staging'), '-E', 'LD_LIBRARY_PATH=' + str(runtime / 'lib'), str(private_copy)]
    log = subprocess.check_output(command, cwd=folder, env=env, text=True, stderr=subprocess.STDOUT, timeout=180)
    (folder / 'production-preflight.log').write_text(log)
    assert 'Frames=30 Rendered=30' in log and 'OriginalEngineTest=FINISHED' in log
    assert cache.current() == private_copy and sha(private_copy) == sha(game)
    private_copy.unlink()
    return dict(passed=True, unmodified_production_platform=True, rendered_preflight_frames=30,
        private_cache_readback=True, wrong_recipe_rejected=True, no_original_required=True,
        game_sha256=sha(game), game_duplicate_removed=True, bundled_backend_cached_only=True,
        physical_display_audio_not_tested=True)


def prepare(package, package_sha, base_sources, engine, raster, output):
    output = output.resolve()
    if sha(package) != package_sha:
        raise ValueError('Base public package hash mismatch')
    engine_report = json.loads((engine / 'report.json').read_text())
    builds = json.loads((engine / 'build.json').read_text())
    raster_report = json.loads((raster / 'report.json').read_text())
    if not engine_report.get('passed') or not engine_report.get('pixel_state_audio_equal') or not raster_report.get('passed'):
        raise ValueError('Renderer/actual-engine equivalence gates failed')
    if engine_report.get('raster_after_over_before', 2) >= 1:
        raise ValueError('No measured actual-engine raster improvement')
    if builds['after']['renderer_sha256'] != raster_report['after_sha256']:
        raise ValueError('Engine and raster tested different renderer sources')
    paired = {}
    for before in engine_report['runs']:
        if before['version'] == 'before' and before['name'].startswith('256-'):
            after = next(r for r in engine_report['runs'] if r['name'] == before['name'].replace('before', 'after'))
            paired[before['name']] = after['raster_ms'] / before['raster_ms']
    if len(paired) != 3:
        raise ValueError('Expected three interleaved full-render default-size comparisons')
    engine_report['paired_256_after_over_before'] = paired
    engine_report['median_paired_256_after_over_before'] = statistics.median(paired.values())
    output.mkdir(exist_ok=False, parents=True)
    source = output / 'source.tar.gz'
    with zipfile.ZipFile(package) as base:
        manifest = json.loads(base.read('runtime/distribution-manifest.json'))
        verified_members(base, manifest)
        materials = json.loads(base.read('runtime/source-material-review.json'))
        delivery = materials['external_corresponding_source']
        with base.open('runtime/source.tar.gz') as reader:
            changed = rewrite_source(reader, source, builds['reference_source_sha256'], builds['after']['renderer_sha256'])
        with tarfile.open(source) as archive:
            renderer = archive.extractfile(RENDERER).read()
        supplement = output / ('SM64-LilSmoke-' + VERSION + '-corresponding-source.zip')
        source_delivery = source_supplement(supplement, base_sources, delivery, renderer,
            {'engine': engine_report, 'raster': raster_report})
        meta = json.loads(base.read('meta.json'))
        meta.update(version=VERSION, release_label='Local Development: Exact Block Occlusion')
        recipe = json.loads(base.read('runtime/recipe.json'))
        old_recipe_sha = hashlib.sha256(base.read('runtime/recipe.json')).hexdigest()
        recipe = performance_recipe(recipe, sha(source))
        materials['local_performance_source_supplement'] = source_delivery
        offer = (
            'SM64-LilSmoke ' + VERSION + ' - LOCAL DEVELOPMENT, NOT PUBLISHED\n\n'
            'Matching source consists of the unchanged complete base offer plus\n'
            'the separate corresponding-source-performance.zip supplement.\n'
            'Neither source download is required to install or play.\n\n'
            'Complete base source: ' + delivery['url'] + '\nSHA-256: ' + delivery['sha256'] + '\n\n'
            'Local supplement: ' + supplement.name + '\nSHA-256: ' + source_delivery['sha256'] + '\n\n'
            'The local supplement is delivered beside this package, not at a new\n'
            'GitHub release URL. It contains the changed renderer and build/tests.\n'
            'Use both source archives together; the supplement alone is incomplete.\n'
            'All component licenses/notices remain in the base source and runtime.\n'
            'No ROM, extracted assets or cooked game/renderer binary is distributed.\n')
        changes = {'meta.json': json_bytes(meta), 'runtime/recipe.json': json_bytes(recipe),
            'SOURCE-OFFER.txt': offer.encode(), 'runtime/source-material-review.json': json_bytes(materials)}
        for name, data in changes.items():
            manifest['files'][name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        manifest['files']['runtime/source.tar.gz'] = dict(bytes=source.stat().st_size, sha256=sha(source))
        manifest['local_performance_source_supplement'] = source_delivery
        target = output / ('SM64-LilSmoke-' + VERSION + '-LOCAL.lilpck')
        with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name in sorted(manifest['files']):
                if name in changes:
                    archive.writestr(name, changes[name])
                elif name == 'runtime/source.tar.gz':
                    archive.write(source, name)
                else:
                    copy_member(base, name, archive)
            archive.writestr('runtime/distribution-manifest.json', json_bytes(manifest))
    with zipfile.ZipFile(target) as archive:
        verified_members(archive, manifest)
        if archive.testzip() is not None or any(n.startswith(('.sm64-cache/', 'saves/')) for n in archive.namelist()):
            raise ValueError('Invalid/private package member')
        scripts = [n for n in archive.namelist() if '/' not in n and n.endswith('.sh')]
        if scripts != ['SM64.sh'] or any(n.startswith('runtime/corresponding-source/') for n in archive.namelist()):
            raise ValueError('Wrong launcher or source offer bundled into lean package')
        with zipfile.ZipFile(package) as original:
            source_sets = []
            for reader in (original, archive):
                with reader.open('runtime/source.tar.gz') as zipped, tarfile.open(fileobj=zipped, mode='r:gz') as tar:
                    source_sets.append({item.name: hashlib.file_digest(tar.extractfile(item), 'sha256').hexdigest() for item in tar})
            if source_sets[0].keys() != source_sets[1].keys() or sorted(
                    name for name in source_sets[0] if source_sets[0][name] != source_sets[1][name]) != [RENDERER]:
                raise ValueError('An unrelated operational source/helper changed')
    production = engine / builds['production']['binary']
    if sha(production) != builds['production']['sha256']:
        raise ValueError('Private locally linked production game changed')
    new_recipe_sha = hashlib.sha256(changes['runtime/recipe.json']).hexdigest()
    # Link commands already identify the exact read-only runtime library set.
    link = json.loads((engine / 'after-link-command.json').read_text())
    runtime = next(Path(arg[2:]).parent for arg in link if arg.startswith('-L') and arg.endswith('/runtime/lib'))
    with zipfile.ZipFile(target) as archive:
        backend_files = {name: archive.read('runtime/' + name) for name in
            ('native_build.py', 'cook.py', 'resume.py', 'build_progress.py')}
    prepared_test = verify_prepared_game(production.resolve(), runtime, output / 'PRIVATE-cache-qa',
        changes['runtime/recipe.json'], old_recipe_sha, backend_files)
    (output / 'prepared-qa-report.json').write_bytes(json_bytes(prepared_test))
    private = dict(schema=1, app_id='org.lilsmoke.sm64', expected_installed_version='0.0.1-prep.8',
        local_only=True, contains_private_game_data=True, distributable=False,
        expected_installed_recipe_sha256=old_recipe_sha, expected_installed_game_sha256=builds['cached_game_sha256'],
        replacement_game_path=str(production.resolve()), replacement_game_sha256=sha(production),
        new_recipe_sha256=new_recipe_sha, new_source_sha256=sha(source),
        renderer_object_sha256=builds['production']['renderer_object_sha256'],
        deployment_performed=False, requires_parent_transactional_staging=True,
        new_current=dict(schema=1, generation='ready-performance-20261026', recipe=new_recipe_sha,
            game_sha256=sha(production), rom_sha1='9bef1128717f958171a4afac3ed78ee2bb4e86ce', extracted_bytes=10679837))
    (output / 'PRIVATE-prepared-update.json').write_bytes(json_bytes(private))
    report = dict(schema=1, version=VERSION, local_development_only=True, uploaded=False,
        package=target.name, bytes=target.stat().st_size, sha256=sha(target),
        base_package_sha256=package_sha, changed_distribution_files=sorted([*changes, 'runtime/source.tar.gz', 'runtime/distribution-manifest.json']),
        changed_engine_files=changed, source_supplement=source_delivery,
        old_recipe_sha256=old_recipe_sha, new_recipe_sha256=new_recipe_sha,
        rom_included=False, extracted_assets_included=False, cooked_game_included=False,
        renderer_binary_included=False, existing_ready_cache_requires_explicit_private_update_or_rebuild=True,
        median_paired_qemu_raster_reduction_percent=100*(1-statistics.median(paired.values())),
        all_other_operational_source_and_helper_bytes_unchanged=True,
        physical_fps_measured=False, whole_system_installer_qualification=False)
    (output / 'package-report.json').write_bytes(json_bytes(report))
    for file in (target, supplement):
        file.with_suffix(file.suffix + '.sha256').write_text(sha(file) + '  ' + file.name + '\n')
    source.unlink()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'base-sources', 'engine', 'raster', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--package-sha256', required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.package, args.package_sha256, args.base_sources, args.engine, args.raster, args.out), indent=2))


if __name__ == '__main__':
    main()
