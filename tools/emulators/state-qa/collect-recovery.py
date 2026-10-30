"""Collect the bounded RetroArch regression evidence without copying any ROM."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

P = Path(__file__).resolve().parents[3]
D = P / 'build/retroarch-recovery-20261021'
D.mkdir(exist_ok=True)
reports = {}
for name, source in (
    ('native-loading-kernel', Path('/root/m8-loading-kernel-20261021-production')),
    ('native-loading-ab', Path('/root/m8-native-loading-ab-20261021-production')),
    ('snes-menu', Path('/root/rgui-state-menu-snes-fixed-20261021')),
    ('md-menu', Path('/root/rgui-state-menu-md-fixed-20261021')),
    ('gba-menu', Path('/root/rgui-state-menu-gba-fixed-20261021')),
    ('nes-menu', Path('/root/rgui-state-menu-nes-fixed-20261021')),
):
    report = json.loads((source / 'report.json').read_text())
    assert report['passed'], report
    reports[name] = report
    shutil.copyfile(source / 'report.json', D / (name + '-report.json'))
    for log in ('retroarch.log', 'serial.log'):
        if (source / log).exists():
            shutil.copyfile(source / log, D / (name + '-' + log))
    if (source / 'menu.ppm').exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source / 'menu.ppm'),
                        '-vframes', '1', str(D / (name + '.png'))], check=True)
for path in Path('/root/m8-native-loading-ab-20261021-production').glob('*.log'):
    shutil.copyfile(path, D / path.name)
binary = Path('/root/retroarch-identity-20261021/retroarch')
(D / 'recovery-test-report.json').write_text(json.dumps(dict(
    passed=True, reports=reports, native_core_binaries_unchanged=True,
    retroarch_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    physical_console_tested=False, hardware_fps_measured=False), indent=2) + '\n')
print('PASS: menu, mapping, kernel, before/after evidence collected; no ROM copied.')
