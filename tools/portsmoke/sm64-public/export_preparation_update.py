#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Export the tested asset-free update and a separate private resume index."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

from build_progress import classify_command
from cook import digest_file

PAYLOAD = ('meta.json', 'runtime/cook.py', 'runtime/native_build.py',
           'runtime/resume.py', 'runtime/build_progress.py',
           'runtime/bin/sm64-prepare', 'runtime/bin/sm64-bin2c',
           'runtime/distribution-manifest.json')


def export(stage, native, vm, installer, legacy, out):
    reports = {}
    for name, root in (('native', native), ('ui-vm', vm), ('installer-vm', installer)):
        report = json.loads((root / 'report.json').read_text())
        if report.get('passed') is not True:
            raise ValueError('Acceptance failed: ' + name)
        reports[name] = report
    required = {
        'native': ('full_native_mips_build', 'legacy_source_verified',
                   'cancellation_preserves_original', 'resumes_without_reextracting',
                   'texture_progress', 'compilation_progress', 'repeat_launch',
                   'deletes_only_committed_app_original', 'owner_rom_preserved'),
        'ui-vm': ('production_extraction_ui', 'real_rom_extraction',
                  'cancel_preserves_original', 'resumes_without_reextracting', 'real_build_progress'),
        'installer-vm': ('production_package_inspect', 'production_package_install',
                         'all_immutable_file_hash_readback', 'single_root_script',
                         'rom_absent', 'cooked_game_absent', 'storage_unmounted_cleanly'),
    }
    for name, keys in required.items():
        if any(reports[name].get(key) is not True for key in keys):
            raise ValueError('Incomplete acceptance: ' + name)
    if reports['native'].get('rendered_preflight_frames') != 30:
        raise ValueError('Native renderer preflight missing')
    progress_audit = json.loads((native / 'progress-audit.json').read_text())
    if progress_audit.get('passed') is not True or progress_audit.get('percent') != 100:
        raise ValueError('Real build progress did not reconcile')
    reports['progress-audit'] = progress_audit
    package = json.loads((stage / 'package-report.json').read_text())
    if any(package.get(key) is not False for key in
           ('rom_included', 'cooked_game_included', 'renderer_binary_included')):
        raise ValueError('Package asset-exclusion review missing')
    app = stage / 'Super Mario 64'
    for name, expected in reports['native']['runtime_sources'].items():
        if digest_file(app / 'runtime' / name) != expected:
            raise ValueError('Tested preparation code changed: ' + name)
    archive = stage / package['package']
    if archive.name != package['package'] or digest_file(archive) != package['sha256']:
        raise ValueError('Package identity changed')
    with tarfile.open(legacy, 'r:') as incoming:
        old_makefile = incoming.extractfile('./source/Makefile').read()
        old_log = incoming.extractfile('./build.log').read().decode(errors='replace')
    previous = None
    for line in old_log.splitlines():
        task = classify_command(line)
        if task and task.target:
            previous = task
    if previous is None or not previous.target.startswith('build/original-mips/'):
        raise ValueError('Cannot identify the interrupted generated output')
    reference = native / 'reference'
    marker = json.loads((reference / 'assets-ready.json').read_text())
    if marker['recipe'] != json.loads((native / 'legacy-verification.json').read_text())['recipe']:
        raise ValueError('Private checkpoint identity differs')
    out.mkdir(parents=True, exist_ok=False)
    for name in PAYLOAD:
        target = out / 'payload' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(app / name, target)
    private = out / 'PRIVATE-resume'
    private.mkdir()
    shutil.copyfile(reference / 'assets-ready.json', private / 'assets-ready.json')
    shutil.copyfile(reference / 'source/Makefile', private / 'Makefile')
    (private / 'inflight.json').write_text(json.dumps(dict(schema=1, target=previous.target)) + '\n')
    (private / 'active.json').write_text(json.dumps(dict(schema=1, recipe=marker['recipe'],
        rom_sha1=marker['rom_sha1'], generation='working-qy1oayh4')) + '\n')
    shutil.copyfile(archive, out / archive.name)
    for name, report in reports.items():
        (out / (name + '-report.json')).write_text(json.dumps(report, indent=2) + '\n')
    for name in ('unpacking', 'assets-extracted', 'resumed-build'):
        shutil.copyfile(vm / (name + '.png'), out / (name + '.png'))
    result = dict(schema=1, passed=True, package=archive.name,
        package_sha256=package['sha256'], package_bytes=archive.stat().st_size,
        old_private_makefile_sha256=hashlib.sha256(old_makefile).hexdigest(),
        payload={name: dict(sha256=digest_file(out / 'payload' / name),
                           bytes=(out / 'payload' / name).stat().st_size) for name in PAYLOAD},
        private={name: digest_file(private / name) for name in
                 ('assets-ready.json', 'Makefile', 'inflight.json', 'active.json')},
        rom_excluded_from_package=True, cooked_game_excluded_from_package=True,
        private_resume_index_not_for_publication=True)
    (out / 'deployment.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'native', 'vm', 'installer', 'legacy', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(json.dumps(export(**vars(parser.parse_args())), indent=2))
