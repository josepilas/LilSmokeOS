"""Verify and collect isolated follow-up artifacts; never touches SDK or cards."""
import hashlib
import json
from pathlib import Path
import shutil
from PIL import Image

P = Path(__file__).resolve().parents[3]
Q = Path('/root/lilsmoke-state-qa-20261021')
F = Path('/root/lilsmoke-state-qa-20261021-final')
out = P / 'build/save-state-qa-20261021-final'
out.mkdir(parents=True, exist_ok=True)
package = F / 'PCSX4ALL-1.0.4.lilpck'
metadata = json.loads(package.with_suffix('.json').read_text())
assert hashlib.sha256(package.read_bytes()).hexdigest() == metadata['package_sha256']
assert hashlib.sha256((F / 'pcsx4all-launch').read_bytes()).hexdigest() == metadata['core_binary_sha256']
core = Path('/root/lilsmoke-psx-20261010/native/libpcsx4all_rkgame.a')
assert hashlib.sha256(core.read_bytes()).hexdigest() == metadata['engine_archive_sha256']
assert metadata['qualified_psx_archive_preserved']
for name in ('PCSX4ALL-1.0.4.lilpck', 'PCSX4ALL-1.0.4.json', 'pcsx4all-launch'):
    shutil.copy2(F / name, out / name)
native = json.loads((Q / 'native-build-report.json').read_text())
for record in native:
    binary = Path(record['path'])
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == record['sha256']
    destination = out / 'native-stage' / record['install_path'].lstrip('/')
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination)
for name in ('native-build-report.json', 'race-build.log', 'pokemini-build.log',
             'race-dynamic.txt', 'pokemini-dynamic.txt', 'serial.log', 'memory.txt',
             'pcsx-roundtrip.log', 'pcsx-roundtrip.exit', 'libretro-roundtrip.log', 'libretro-roundtrip.exit',
             'pcsx-menu-ui-surface.log', 'pcsx-menu-ui-surface.exit',
             'pcsx-menu-ui-renderer.log', 'pcsx-menu-ui-renderer.exit'):
    shutil.copy2(Q / name, out / name)
for engine in ('pcsx', 'libretro'):
    assert (Q / (engine + '-roundtrip.exit')).read_text().strip() == '0'
ui = {}
for mode in ('surface', 'renderer'):
    log = (Q / ('pcsx-menu-ui-' + mode + '.log')).read_text()
    assert (Q / ('pcsx-menu-ui-' + mode + '.exit')).read_text().strip() == '0'
    assert 'PCSX_PAUSE_UI_PASS mode=' + mode in log
    assert 'PCSX_OWN_MENU_EXEC_ARGV_PASS' in log
    images = []
    for bmp in sorted(Q.glob('pcsx-ui-' + mode + '-*.bmp')):
        target = out / (bmp.stem + '.png')
        Image.open(bmp).save(target)
        images.append(target.name)
    assert len(images) == 7
    ui[mode] = dict(passed=True, actual_production_menu=True, actual_pinned_serializer=True,
                    synthetic_input_and_video=True, exit_action=True, driver_teardown_tested=False,
                    screenshots=images)
report = dict(package_sha256=metadata['package_sha256'], frontend_sha256=metadata['core_binary_sha256'],
              core_sha256=metadata['engine_archive_sha256'], core_preserved=True,
              native=native, ui=ui, real_core_roundtrips=True,
              guest_ram_mib=120, cpus=1, gpu=False, shared_sdk_written=False, sd_card_written=False,
              active_buildroot_native_packages_enabled=False,
              native_payload_status='optional cached payload present; no physical-card installation inspected')
(out / 'followup-report.json').write_text(json.dumps(report, indent=2) + '\n')
(F / 'followup-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(output=str(out), package_sha256=metadata['package_sha256'],
                      native_install_paths=[r['install_path'] for r in native], ui_modes_passed=list(ui)), indent=2))
