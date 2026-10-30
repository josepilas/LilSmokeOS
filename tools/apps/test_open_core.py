"""Disposable package/provider/launch tests; never access a physical card."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--router', type=Path)
p.add_argument('--qemu', action='store_true')
p.add_argument('--sysroot', type=Path)
a = p.parse_args()
command = [str(a.binary)]
if a.qemu:
    command = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(a.sysroot), *command]
count = 0
with tempfile.TemporaryDirectory(prefix='lilsmoke-open-core-') as tmp:
    base = Path(tmp)
    root = base / 'independent-apps'
    roms = base / 'roms'
    (roms / 'gba' / 'nested').mkdir(parents=True)
    (roms / 'md').mkdir()
    odd_name = "Name with spaces;$(touch INJECTION).gba"
    packed = roms / 'gba' / 'nested' / odd_name
    packed.write_bytes(b'private test fixture')
    archive = roms / 'gba' / 'packed.7z'
    archive.write_bytes(b'not extracted: app owns archive resolution')
    result = base / 'result'
    env = dict(os.environ, LILSMOKE_APPS_ROOT=str(root), LILSMOKE_ROM_ROOT=str(roms),
               LILSMOKE_APPS_HELPER=str(a.binary), QA_RESULT=str(result),
               LILSMOKE_HCFB='0')
    if a.qemu:
        env['LD_LIBRARY_PATH'] = str(a.sysroot / 'usr/lib') + ':' + str(a.sysroot / 'lib')

    def call(*words, ok=True, binary=None):
        global count
        run = subprocess.run([*(command if binary is None else [str(binary)]), *map(str, words)],
                             env=env, text=True, capture_output=True, timeout=40)
        assert (run.returncode == 0) == ok, (words, run.stdout, run.stderr)
        count += 1
        return run.stdout

    def package(label, cores, version='1.0.0', identity='org.test.multi'):
        path = base / (label + '.lilpck')
        meta = dict(name='Multi Emulator', id=identity, version=version, open_core=cores)
        script = '''#!/bin/sh
set -eu
printf '%s\\n' "$LILSMOKE_APP_NAME" "${core-}" "${rom-}" "${LILSMOKE_OPEN_CORE-}" "${LILSMOKE_OPEN_ROM-}" "$#" "$@" > "$QA_RESULT"
'''
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('meta.json', json.dumps(meta))
            z.writestr('run.sh', script)
        return path

    one = package('one', 'gba')
    call('install', one)
    assert call('emulators').strip().split('\t') == ['org.test.multi', 'Multi Emulator', 'gba', '1.0.0']
    two = package('two', ['GBA', 'genesis', 'gba'], '2.0.0')
    call('install', two)
    assert call('emulators').strip().split('\t')[2] == 'gba,md'
    call('rename', 'Multi Emulator', 'Renamed Emulator')
    assert call('emulators').strip().split('\t')[:2] == ['org.test.multi', 'Renamed Emulator']
    if not a.qemu:
        call('run-core', 'org.test.multi', 'gba', packed)
        logical = 'nested/' + odd_name
        assert result.read_text().splitlines() == ['Renamed Emulator', 'gba', logical, 'gba', logical,
                                                  '2', 'core=gba', 'rom=' + logical]
        assert not (root / 'Renamed Emulator' / 'INJECTION').exists()
        call('run-core', 'org.test.multi', 'gba', 'packed.7z')
        assert result.read_text().splitlines()[2] == 'packed.7z'
        env.update(core='stale', rom='/absolute/stale', LILSMOKE_OPEN_CORE='stale', LILSMOKE_OPEN_ROM='stale')
        call('run', 'Renamed Emulator')
        assert result.read_text().splitlines() == ['Renamed Emulator', '', '', '', '', '0']
        for identity, system, filename in [('missing', 'gba', packed), ('org.test.multi', 'snes', packed),
                                          ('org.test.multi', 'md', packed), ('org.test.multi', 'gba', '../outside.gba')]:
            call('run-core', identity, system, filename, ok=False)
        (base / 'outside.gba').write_bytes(b'outside')
        (roms / 'gba' / 'link.gba').symlink_to(base / 'outside.gba')
        call('run-core', 'org.test.multi', 'gba', 'link.gba', ok=False)
        (roms / 'gba' / 'outside').symlink_to(base, target_is_directory=True)
        call('run-core', 'org.test.multi', 'gba', 'outside/outside.gba', ok=False)
        if a.router:
            config = base / 'config'
            config.write_text('EMULATOR_gba="app:org.test.multi"\n')
            env['LILSMOKE_SYSTEM_CONFIG'] = str(config)
            assert call('resolve', 'gba', binary=a.router).strip() == 'app:org.test.multi'
            call('run-provider', 'app:org.test.multi', 'gba', archive, binary=a.router)
            assert result.read_text().splitlines()[2] == 'packed.7z'
    for i, invalid in enumerate([[], ['gba', 7], ['unknown'], None, 1, {}, 'gba\x00md']):
        call('inspect', package('invalid' + str(i), invalid), base / 'preview', ok=False)
    # A duplicated ID must never silently choose a different application's code.
    import shutil
    duplicate = root / 'Duplicate'
    shutil.copytree(root / 'Renamed Emulator', duplicate)
    meta = json.loads((duplicate / 'meta.json').read_text())
    meta['name'] = 'Duplicate'
    (duplicate / 'meta.json').write_text(json.dumps(meta))
    assert not call('emulators').strip()
    call('run-core', 'org.test.multi', 'gba', archive, ok=False)
    shutil.rmtree(duplicate)
    call('uninstall', 'Renamed Emulator')
    assert not call('emulators').strip()
    call('seed', one)
    call('seed', one)
    assert (root/'Multi Emulator/run.sh').is_file()
    (root/'Multi Emulator/player.sav').write_text('keep my save')
    call('rename', 'Multi Emulator', 'Seed Renamed')
    call('seed', two)
    assert json.loads((root/'Seed Renamed/meta.json').read_text())['version']=='2.0.0'
    assert (root/'Seed Renamed/player.sav').read_text()=='keep my save'
    call('seed', one)
    assert json.loads((root/'Seed Renamed/meta.json').read_text())['version']=='2.0.0'
    call('uninstall', 'Seed Renamed')
    call('seed', two)
    assert not call('emulators').strip(),'Builtin seeding must not undo an intentional uninstall'
print(f'PASS {count} open_core package/catalog/rename/update/security operations ({"MIPS" if a.qemu else "host"})')
