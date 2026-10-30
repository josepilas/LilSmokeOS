#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Export a tested prep8 update that preserves an already prepared game."""
import argparse
import json
from pathlib import Path
import shutil
from cook import digest_file
from audit_runtime import audit

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--staging', type=Path, required=True)
parser.add_argument('--cached-vm', type=Path, required=True)
parser.add_argument('--installer-report', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
app = args.staging / 'Super Mario 64'
package = json.loads((args.staging / 'package-report.json').read_text())
manifest_path = app / 'runtime/distribution-manifest.json'
manifest = json.loads(manifest_path.read_text())
vm = json.loads(args.cached_vm.read_text())
installer = json.loads(args.installer_report.read_text())
assert vm.get('passed') is True and installer.get('passed') is True
assert vm.get('cached_without_compilation') and vm.get('repeat_launch') and vm.get('filesystem_clean')
assert installer.get('prepared_cache_preservation') and installer.get('changed_recipe_and_game_rejected')
for key, name in (('bootstrap_sha256', 'runtime/bin/sm64-prepare'),
                  ('backend_sha256', 'runtime/native_build.py'),
                  ('bundled_sdl_sha256', 'runtime/lib/libSDL2-2.0.so.0')):
    assert vm[key] == manifest['files'][name]['sha256'] == digest_file(app / name)
assert not package['rom_included'] and not package['cooked_game_included']
assert not package['renderer_binary_included']
assert digest_file(args.staging / package['package']) == package['sha256']
closure = audit(app, args.sdk)
assert closure['passed'], closure
args.out.mkdir(exist_ok=False)
for source, name in ((args.staging / package['package'], package['package']),
                     (manifest_path, 'distribution-manifest.json'),
                     (args.cached_vm, 'cached-vm-report.json'),
                     (args.installer_report, 'installer-report.json')):
    target = args.out / name
    shutil.copyfile(source, target)
    assert digest_file(source) == digest_file(target)
report = dict(schema=1, passed=True, version='0.0.1-prep.8',
    fresh_install=False, app_id='org.lilsmoke.sm64', rom_included=False,
    cooked_game_included=False, renderer_binary_included=False,
    package=package['package'], sha256=package['sha256'], files=package['files'],
    manifest_sha256=digest_file(manifest_path), preserves_prepared_cache=True)
(args.out / 'deployment.json').write_text(json.dumps(report, indent=2) + '\n')
(args.out / 'runtime-audit.json').write_text(json.dumps(closure, indent=2) + '\n')
print(json.dumps(report, indent=2))

