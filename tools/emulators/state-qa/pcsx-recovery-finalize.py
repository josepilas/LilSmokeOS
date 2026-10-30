"""Recheck acceptance evidence and emit a final, non-deploying integration manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

P = Path(__file__).resolve().parents[3]
Q = Path('/root/pcsx-state-recovery-20261021')
CORE = Path('/root/lilsmoke-psx-20261010/native/libpcsx4all_rkgame.a')
QUALIFIED = '1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--run', type=Path, required=True)
a = parser.parse_args()
run = a.run.resolve()
assert run.is_relative_to(Q) and run != Q
result = json.loads((run / 'pcsx-recovery-results.json').read_text())
cases = {'baseline', 'baseline-enosys', 'normal', 'sync-enosys', 'all-sync-enosys',
         'io-error', 'rename-error', 'enospc', 'gpu-oom', 'screenshot-oom'}
assert result['passed'] is True and result['production_menu'] is True
assert set(result['cases']) == cases and len(result['cases']) == len(cases)
for case in cases:
    assert (run / (case + '.exit')).read_text().strip() == '0'
    assert 'PCSX_RECOVERY_PASS scenario=' + case in (run / (case + '.log')).read_text(errors='replace')
for mode in ('surface', 'renderer'):
    assert (run / f'menu-{mode}.exit').read_text().strip() == '0'
    assert 'PCSX_RECOVERY_MENU_PASS mode=' + mode in (run / f'menu-{mode}.log').read_text(errors='replace')

def sha256(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()

assert sha256(CORE) == result['core_archive_sha256'] == QUALIFIED
assert sha256(run / 'pcsx-recovery-engine') == result['engine_test_sha256']
subprocess.run([sys.executable, str(P / 'tools/emulators/state-qa/pcsx-recovery-package-test.py')], check=True)
package = Q / 'PCSX4ALL-1.0.5.lilpck'
info = json.loads(package.with_suffix('.json').read_text())
assert sha256(package) == info['package_sha256']
assert sha256(Q / 'pcsx4all-launch') == info['core_binary_sha256']
manifest = dict(status='finalized-for-integration', version='1.0.5',
                package=str(package), package_bytes=package.stat().st_size,
                package_sha256=info['package_sha256'],
                frontend=str(Q / 'pcsx4all-launch'), frontend_sha256=info['core_binary_sha256'],
                qualified_core_sha256=QUALIFIED, acceptance_evidence=str(run),
                engine_cases_passed=10, production_menu_modes_passed=['surface', 'renderer'],
                package_regression_passed=True, physical_vendor_kernel_tested=False,
                hardware_acceptance_pending=True, sd_deployment_performed=False)
destination = Q / 'pcsx-recovery-final.json'
with destination.open('x') as file:
    json.dump(manifest, file, indent=2)
    file.write('\n')
print(json.dumps(manifest, indent=2))
