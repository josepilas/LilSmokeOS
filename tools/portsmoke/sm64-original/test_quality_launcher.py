"""Run the actual launcher with an inert executable and isolated log/storage paths."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
source = (ROOT / 'package/lilsmoke-sm64-original/local-app/SM64.sh').read_text()
out = HERE / '_build/quality-20261012'
out.mkdir(exist_ok=True)
results = []
with tempfile.TemporaryDirectory(prefix='launcher-', dir=out) as temporary:
    root = Path(temporary)
    app = root / 'app'
    (app / 'saves').mkdir(parents=True)
    (app / 'runtime/bin').mkdir(parents=True)
    stub = app / 'runtime/bin/sm64-original'
    stub.write_text('#!/bin/sh\n/usr/bin/env\n')
    stub.chmod(0o755)
    reader = root / 'config-read'
    reader.write_text('#!/bin/sh\nprintf "80\\n"\n')
    reader.chmod(0o755)
    launcher = root / 'SM64.sh'
    launcher.write_text(source.replace('/persist/logs', str(root / 'logs'))
        .replace('/run/lilsmoke-storage/exporting', str(root / 'exporting')))
    clean = {key: value for key, value in os.environ.items()
        if not key.startswith(('SM64_', 'LILSMOKE_', 'LD_'))}
    base = dict(clean, LILSMOKE_APP_DIR=str(app),
        LILSMOKE_APP_ID='org.lilsmoke.sm64.original.local',
        LILSMOKE_CONFIG_READER=str(reader), LILSMOKE_HCFB='0',
        SM64_ORIGINAL_TEST_FRAMES='3600', SM64_ORIGINAL_RENDER_EVERY='15',
        SM64_ORIGINAL_TEST_WIDTH_CYCLE='1', SM64_ORIGINAL_TEST_WORLD_EVIDENCE='1')
    for name, saved, expected in (
        ('default', {}, {}),
        ('detail', {'world-quality.txt': 'detail\n'}, {'SM64_WORLD_QUALITY': 'detail'}),
        ('adaptive-smooth', {'world-quality.txt': 'adaptive\n', 'world-scale.txt': 'smooth\n'},
            {'SM64_WORLD_QUALITY': 'adaptive', 'SM64_WORLD_SCALE': 'smooth'}),
        ('width-priority', {'world-quality.txt': 'adaptive\n', 'render-width.txt': '192\n'},
            {'SM64_WORLD_QUALITY': 'adaptive', 'SM64_SOFTWARE_WIDTH': '192'}),
        ('menu-settings', {'render-width.txt':'192\n','.sm64-port/lilsmoke-options.conf':'Quality=detail\nScale=smooth\nVolume=95\n'},
            {'SM64_WORLD_QUALITY':'detail','SM64_WORLD_SCALE':'smooth','SM64_APP_VOLUME':'95'}),
        ('invalid-saved', {'world-quality.txt': 'invalid\n', 'world-scale.txt': 'blur\n', 'render-width.txt': '240\n'}, {}),
    ):
        for file in (app / 'saves').glob('*.txt'):
            file.unlink()
        (app/'saves/.sm64-port/lilsmoke-options.conf').unlink(missing_ok=True)
        for filename, value in saved.items():
            (app/'saves'/filename).parent.mkdir(parents=True,exist_ok=True)
            (app / 'saves' / filename).write_text(value)
        log = root / 'logs/sm64-original-local.log'
        log.unlink(missing_ok=True)
        subprocess.run(['sh', str(launcher)], env=base, check=True, timeout=10)
        values = dict(line.split('=', 1) for line in log.read_text().splitlines() if '=' in line)
        for key, value in expected.items():
            assert values[key] == value, name
        for key in ('SM64_WORLD_QUALITY', 'SM64_WORLD_SCALE', 'SM64_SOFTWARE_WIDTH','SM64_APP_VOLUME'):
            if key not in expected:
                assert key not in values, (name, key)
        assert not any(key.startswith('SM64_ORIGINAL_') for key in values), name
        assert values['SDL_VIDEODRIVER'] == 'directfb'
        assert values['SDL_RENDER_DRIVER'] == 'software' and values['LILSMOKE_M8_AUDIO'] == '1'
        assert values['LILSMOKE_M8_VOLUME'] == '40'
        assert values['HOME'] == str(app / 'saves')
        results.append(name)
    (root / 'exporting').touch()
    assert subprocess.run(['sh', str(launcher)], env=base, capture_output=True).returncode == 1
    (root / 'exporting').unlink()
    assert subprocess.run(['sh', str(launcher)], env=dict(base, LILSMOKE_APP_ID='wrong'),
        capture_output=True).returncode == 2
(out / 'launcher.json').write_text(json.dumps(dict(passed=True, cases=results,
    qa_hooks_removed=True, directfb_native_audio_preserved=True, storage_export_guard=True,
    no_real_device_or_host_input=True), indent=2) + '\n')
print('PASS actual launcher defaults/options/legacy width/invalid files, QA stripping, native adapters, export/id guards')
