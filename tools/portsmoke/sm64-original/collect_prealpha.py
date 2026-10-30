# SPDX-License-Identifier: MPL-2.0
"""Private port closeout evidence; does not mark a public port complete."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
base=Path('/root/sm64-prealpha-20261012')
out=ROOT/'test-results/pre-alpha-0.0.1'
out.mkdir(exist_ok=True)
host=json.loads((base/'engine-regression/report.json').read_text())
menu=json.loads((base/'vm-menu/report.json').read_text())
restart=json.loads((base/'vm-restart/report.json').read_text())
audit=json.loads((HERE/'_build/source-audit.json').read_text())
assert all(r['passed'] for r in (host,menu,restart))
assert host['current_version']=='0.6.0' and host['same_size_pixels_identical_to_040']
assert menu['menu_pause_settings_resume_exercised'] and restart['previous_storage_reused']
relative='storage/independent-apps/Super Mario 64 Local Test/saves/.sm64-port/'
for name in ('lilsmoke-options.conf','sm64_save_file.bin'):
    before=(base/'vm-menu'/relative/name).read_bytes()
    after=(base/'vm-restart'/relative/name).read_bytes()
    assert before==after,name
assert 'quality=fixed scale=smooth' in (base/'vm-restart/engine.log').read_text()
assert audit['engineFilesUnmodified']==2500 and not audit['assetFreeRuntimeImplemented']
package=ROOT/'build/private-local-apps/Super-Mario-64-Local-Test-0.6.0.lilpck'
info=json.loads(package.with_suffix('.json').read_text())
with zipfile.ZipFile(package) as z:
    assert z.testzip() is None and json.loads(z.read('meta.json'))['version']=='0.6.0'
    assert z.read('runtime/licenses/LilSmoke-MPL-2.0.txt').strip()==(ROOT/'LICENSES/MPL-2.0.txt').read_bytes().strip()
    assert hashlib.sha256(z.read('runtime/bin/sm64-original')).hexdigest()==info['binary_sha256']
assert info['binary_sha256']==menu['binary_sha256']==restart['binary_sha256']
assert hashlib.sha256(package.read_bytes()).hexdigest()==info['package_sha256']
rom=Path('/path/to/local-input/Super Mario 64 (USA).z64')
assert hashlib.sha256(rom.read_bytes()).hexdigest()=='17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
for name,path in (('host',base/'engine-regression/report.json'),('mips-menu',base/'vm-menu/report.json'),
    ('mips-restart',base/'vm-restart/report.json'),('launcher',HERE/'_build/quality-20261012/launcher.json'),
    ('quality-units',HERE/'_build/quality-20261012/units/report.json')):
    shutil.copyfile(path,out/(name+'.json'))
shutil.copyfile(base/'vm-menu/menu-screen.png',out/'private-menu-mips.png')
result=dict(passed=True,private_version='0.6.0',release_name='Pre-Alpha-0.0.1',
    host_full_frames=host['fully_rendered_frames'],mips_full_frames=menu['frames']+restart['frames'],
    original_engine_files_unmodified=2500,options_menu_game_logic_frozen=True,settings_and_save_restart_verified=True,
    package_sha256=info['package_sha256'],binary_sha256=info['binary_sha256'],
    original_rom_unchanged=True,public_port_finished=False,public_distribution_allowed=False,
    physical_m8_menu_tested=False,audible_hardware_audio_tested=False)
(out/'sm64-private-closeout.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
