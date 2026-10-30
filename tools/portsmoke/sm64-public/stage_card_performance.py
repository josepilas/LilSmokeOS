#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Update a PC-only copy with the real installer and a private prepared game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from cook import Cache, atomic_json, verify_mips_executable


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Application contains a symlink')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha(path)
        elif not path.is_dir():
            raise ValueError('Unsupported file type')
    return result


def run_stage(work):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'package', 'handoff', 'service'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    project = Path(__file__).resolve().parents[3]
    a.stage = a.stage.resolve()
    if not a.stage.is_relative_to(project / 'build') or not a.stage.is_dir():
        raise ValueError('Only an existing PC build staging directory is allowed')
    app = a.stage / 'independent-apps/Super Mario 64'
    report_path = a.stage / 'prepared-update.json'
    if report_path.exists():
        raise ValueError('Stage was already processed')
    h = json.loads(a.handoff.read_text())
    package_report = json.loads(a.handoff.with_name('package-report.json').read_text())
    meta = json.loads((app / 'meta.json').read_text())
    recipe = app / 'runtime/recipe.json'
    if (meta['id'] != h['app_id'] or meta['version'] != h['expected_installed_version'] or
            sha(recipe) != h['expected_installed_recipe_sha256'] or
            sha(a.package) != package_report['sha256']):
        raise ValueError('Installed app or public package is not the qualified baseline')
    old_cache = Cache(app, sha(recipe)).current()
    if old_cache is None or sha(old_cache) != h['expected_installed_game_sha256']:
        raise ValueError('Prepared game baseline changed')
    game = Path(h['replacement_game_path'])
    if not h['local_only'] or h['distributable'] or sha(game) != h['replacement_game_sha256']:
        raise ValueError('Invalid private prepared update')
    before = inventory(app)
    # DrvFS fsync per extracted file is extremely slow. Use one disposable
    # ext4 work tree, then copy the qualified result back for Windows transfer.
    original_app = app
    app = work / 'independent-apps/Super Mario 64'
    shutil.copytree(original_app, app)
    recipe = app / 'runtime/recipe.json'
    sdk = Path('/root/lilsmoke-os/build/buildroot/output')
    command = ['taskset', '-c', '3', 'nice', '-n', '10', 'qemu-mipsel', '-cpu', '74Kf',
               '-L', str(sdk / 'staging'), '-E', 'LILSMOKE_APPS_ROOT=' + str(app.parent),
               str(a.service.resolve()), 'install', str(a.package.resolve()), sha(a.package)]
    run = subprocess.run(command, capture_output=True, text=True, timeout=600)
    (a.stage / 'installer.log').write_text(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError('Actual LilApps installer rejected update: ' + (run.stdout + run.stderr)[-3000:])
    if sha(recipe) != h['new_recipe_sha256']:
        raise ValueError('Installed recipe differs from private prepared game')
    current = h['new_current']
    ready = app / '.sm64-cache' / current['generation']
    ready.mkdir(exist_ok=False)
    target = ready / 'game'
    with game.open('rb') as src, target.open('xb') as dst:
        shutil.copyfileobj(src, dst)
        dst.flush()
        os.fsync(dst.fileno())
    target.chmod(0o700)
    verify_mips_executable(target)
    if sha(target) != current['game_sha256']:
        raise ValueError('Private game readback failed')
    atomic_json(app / '.sm64-cache/current.json', current)
    if Cache(app, sha(recipe)).current() != target:
        raise ValueError('Ordinary cache validator rejected update')
    after = inventory(app)
    # The actual installer owns package contents; game/user data stays unmodified.
    user = {k: v for k, v in before.items() if k.startswith(('saves/', 'original/', '.sm64-cache/'))
            and k != '.sm64-cache/current.json'}
    if any(after.get(k) != v for k, v in user.items()):
        raise ValueError('Existing saves, ROMs or private cache changed')
    with zipfile.ZipFile(a.package) as archive:
        declared = json.loads(archive.read('runtime/distribution-manifest.json'))['files']
        for name, record in declared.items():
            if name == 'meta.json':
                continue  # Installer normalizes JSON/name while preserving identity.
            if after.get(name) != record['sha256']:
                raise ValueError('Installed package file differs: ' + name)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SM64_', 'LILSMOKE_', 'QEMU_'))}
    env.update(HOME=str(app), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SM64_ORIGINAL_PREFLIGHT='1', SM64_ORIGINAL_TEST_FRAMES='30', SM64_ORIGINAL_RENDER_EVERY='1')
    command = ['taskset', '-c', '3', 'nice', '-n', '10', 'qemu-mipsel', '-cpu', '74Kf',
               '-R', '100663296', '-L', str(sdk / 'staging'), '-E',
               'LD_LIBRARY_PATH=' + str(app / 'runtime/lib'), str(target)]
    preflight = subprocess.run(command, cwd=app, env=env, capture_output=True, text=True, timeout=180)
    log = preflight.stdout + preflight.stderr
    (a.stage / 'preflight.log').write_text(log)
    if preflight.returncode or 'Frames=30 Rendered=30' not in log or 'OriginalEngineTest=FINISHED' not in log:
        raise ValueError('Updated installed game preflight failed: ' + log[-3000:])
    final = inventory(app)
    if any(final.get(k) != v for k, v in user.items()):
        raise ValueError('Preflight changed pre-existing user data')
    delivery = a.stage / 'updated/independent-apps/Super Mario 64'
    shutil.copytree(app, delivery)
    if inventory(delivery) != final:
        raise ValueError('PC delivery copy differs from qualified app')
    report = dict(passed=True, id=h['app_id'], old_version=meta['version'],
                  version=json.loads((app / 'meta.json').read_text())['version'],
                  private_game=True, publishable=False, physical_card_written=False,
                  physical_performance_measured=False, original_needed=False,
                  preserved_user_files=len(user), recipe_sha256=sha(recipe), game_sha256=sha(target),
                  before=before, after=final)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS actual installer + private prepared cache + 30-frame production preflight; no card writes')


def main():
    with tempfile.TemporaryDirectory(prefix='lilsmoke-sm64-update-') as work:
        run_stage(Path(work))


if __name__ == '__main__':
    main()
