"""Export verified private detail evidence; no physical-device writes."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
source = Path('/root/sm64-detail-20261012')
output = ROOT/'test-results/sm64-detail-adapter-2026-10-12'
output.mkdir(exist_ok=True)
host_build = json.loads((HERE/'_build/host-build.json').read_text())
mips_build = json.loads((HERE/'_build/mips-build.json').read_text())
package = source/'package/Super-Mario-64-Local-Test-0.5.0.lilpck'
package_info = json.loads(package.with_suffix('.json').read_text())
assert hashlib.sha256(package.read_bytes()).hexdigest() == package_info['package_sha256']
assert package_info['binary_sha256'] == mips_build['binarySha256']
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None
    assert json.loads(archive.read('meta.json'))['version'] == '0.5.0'
    assert hashlib.sha256(archive.read('runtime/bin/sm64-original')).hexdigest() == mips_build['binarySha256']
    assert not any(name.startswith('saves/') or name.endswith(('.z64','.n64','.v64')) for name in archive.namelist())
for name,path in (('units',HERE/'_build/quality-20261012/units/report.json'),
        ('launcher',HERE/'_build/quality-20261012/launcher.json'),
        ('endpoint',source/'endpoint-final/report.json'),('engine-final',source/'engine-final/report.json')):
    report = json.loads(path.read_text())
    assert report['passed']
    if name == 'engine-final': assert report['binary_sha256'] == host_build['binarySha256']
    shutil.copyfile(path,output/(name+'.json'))
for directory,name,width in (('vm-final','mips-default',256),('vm-320-final','mips-320',320)):
    run = source/directory
    report = json.loads((run/'report.json').read_text())
    log = (run/'engine.log').read_text()
    assert report['passed'] and report['binary_sha256'] == mips_build['binarySha256']
    assert f'SM64_VIDEO world={width}x{width*3//4}' in log
    assert 'Frames=3000 Rendered=3000' in log
    # Older harness metadata called the default "balanced". Record the
    # actual observed runtime mode, never infer it from an omitted argument.
    report['world_quality'] = re.search(r'SM64_VIDEO[^\n]+quality=(\w+)',log).group(1)
    if width == 256: assert report['world_quality'] == 'detail'
    if width == 320: assert report['world_quality'] == 'fixed'
    (output/(name+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    shutil.copyfile(run/'engine.log',output/(name+'.log'))
    shutil.copyfile(run/'process.log',output/(name+'-memory.log'))
    shutil.copyfile(run/'frame-2100.png',output/(name+'.png'))
shutil.copyfile(source/'engine-final/comparison.png',output/'sm64-comparison.png')
rom = Path('/path/to/local-input/Super Mario 64 (USA).z64')
assert hashlib.sha256(rom.read_bytes()).hexdigest() == '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
report = dict(passed=True,version='0.5.0',package_sha256=package_info['package_sha256'],
    binary_sha256=mips_build['binarySha256'],fully_rendered_host_frames=61200,
    fully_rendered_mips_frames=6000,source_rom_unchanged=True,auto_installed=False,
    physical_console_fps_measured=False,audible_hardware_audio_tested=False,
    public_distribution_allowed=False)
(output/'sm64-delivery.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS final SM64 detail package and host/MIPS evidence')
