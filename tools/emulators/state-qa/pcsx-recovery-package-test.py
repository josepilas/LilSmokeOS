"""Verify the recovery package and its qualified-core provenance without installation."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
import os

P = Path(__file__).resolve().parents[3]
Q = Path(os.environ.get('PCSX_QA_OUTPUT', '/root/pcsx-state-recovery-20261021'))
version = json.loads((P / 'package/pcsx4all/app/meta.json').read_text())['version']
package = Q / ('PCSX4ALL-' + version + '.lilpck')
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None
    names = archive.namelist()
    meta = json.loads(archive.read('meta.json'))
    info = json.loads(archive.read('runtime/build-info.json'))
    binary = archive.read('runtime/bin/pcsx4all')
    assert meta['version'] == info['version'] == version
    assert info['save_state_slots'] == 10 and info['qualified_psx_archive_preserved'] is True
    assert info['engine_archive_sha256'] == '1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a'
    assert info['rom_or_bios_included'] is False
    assert binary == (Q / 'pcsx4all-launch').read_bytes()
    assert hashlib.sha256(binary).hexdigest() == info['core_binary_sha256']
    assert all(name.startswith(('runtime/bin/', 'runtime/lib/', 'runtime/licenses/', 'runtime/defaults/')) or
               name in ('meta.json', 'icon.webp', 'PCSX4ALL.sh', 'runtime/build-info.json') for name in names), names
    assert all(not name.endswith(('.state', '.iso', '.cue', '.img', '.mcr', '.rom')) for name in names)
report = json.loads(package.with_suffix('.json').read_text())
assert hashlib.sha256(package.read_bytes()).hexdigest() == report['package_sha256']
assert report['core_binary_sha256'] == info['core_binary_sha256']

# Packaging an old frontend under the new version must fail, without an output.
old = Path('/root/lilsmoke-state-qa-20261021-final/pcsx4all-launch')
if old.is_file():
    reject = Q / 'pcsx-recovery-rejected-old.lilpck'
    assert not reject.exists()
    result = subprocess.run([sys.executable, str(P / 'tools/apps/build_pcsx4all.py'),
                             '--binary', str(old), '--output', str(reject),
                             '--core-archive', '/root/lilsmoke-psx-20261010/native/libpcsx4all_rkgame.a'],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode != 0 and 'Recovery-capable frontend required' in result.stderr
    assert not reject.exists()
with tempfile.TemporaryDirectory(prefix='pcsx-recovery-provenance-', dir=Q) as temporary:
    root = Path(temporary)
    unqualified = root / 'unqualified.a'
    unqualified.write_bytes(b'not the qualified archive')
    reject = root / 'rejected.lilpck'
    result = subprocess.run([sys.executable, str(P / 'tools/apps/build_pcsx4all.py'),
                             '--binary', str(Q / 'pcsx4all-launch'), '--output', str(reject),
                             '--core-archive', str(unqualified), '--require-qualified-core'],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode != 0 and 'Refusing unqualified core' in result.stderr
    assert not reject.exists()
print('PCSX_RECOVERY_PACKAGE_PASS', report['package_sha256'], 'files=' + str(len(names)))
