"""Audit private delivery against completed host/VM tests and current source."""
import hashlib
import json
from pathlib import Path
import re
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
Q = HERE / '_build/quality-20261012'
def read(path):
    return json.loads(path.read_text())

host = read(Q / 'host-regression2/report.json')
units = read(Q / 'units/report.json')
launcher = read(Q / 'launcher.json')
audit = read(HERE / '_build/source-audit.json')
assert host['passed'] and units['passed'] and launcher['passed']
assert audit['engineFilesUnmodified'] == 2500
assert not audit['graphicsBinaryRedistributionAllowed']
platform = ROOT / 'package/lilsmoke-sm64-original/src/platform_sdl2.c'
builds = {target: read(HERE / f'_build/{target}-build.json') for target in ('host', 'mips')}
for build in builds.values():
    assert build['platformSha256'] == hashlib.sha256(platform.read_bytes()).hexdigest()
    assert build['binarySha256'] == hashlib.sha256(Path(build['privateBinary']).read_bytes()).hexdigest()
    assert build['romUnchanged'] and not build['distributable']
assert host['binary_sha256'] == builds['host']['binarySha256']
package = Q / 'private-package/Super-Mario-64-Local-Test-0.4.0.lilpck'
package_info = read(package.with_suffix('.json'))
assert package_info['package_sha256'] == hashlib.sha256(package.read_bytes()).hexdigest()
assert package_info['private_user_build'] and not package_info['public_distribution_allowed']
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None
    assert json.loads(archive.read('meta.json'))['version'] == '0.4.0'
    assert hashlib.sha256(archive.read('runtime/bin/sm64-original')).hexdigest() == builds['mips']['binarySha256']
    assert b'Redistributions in binary form are not allowed.' in archive.read('runtime/licenses/graphics-source-only.txt')
    assert b'SM64_WORLD_QUALITY' in archive.read('SM64.sh')

vms = []
states = []
for case in ('vm-default2', 'vm-adaptive2'):
    directory = Q / case
    vm = read(directory / 'report.json')
    assert vm['passed'] and vm['installed_and_run_as_lilsmoke_application']
    assert vm['binary_sha256'] == builds['mips']['binarySha256']
    assert vm['frames'] == 3000 and vm['render_stride'] == 1
    assert vm['cpus'] == 1 and vm['process_address_space_mib'] == 64
    assert not vm['physical_console_fps_measured'] and not vm['hardware_3d']
    text = (directory / 'engine.log').read_text()
    assert 'Frames=3000 Rendered=3000' in text and 'hud=320x240' in text
    states.append(re.findall(r'^OriginalFrame=.*$', text, re.M))
    process = (directory / 'process.log').read_text()
    memory = {field: max(map(int, re.findall(r'^' + field + r':\s+(\d+) kB', process, re.M)))
        for field in ('VmRSS', 'VmSize', 'VmPeak')}
    vms.append(dict(case=case, frames=3000, world_quality=vm['world_quality'],
        quality_transitions=vm['quality_transitions'], sampled_memory_kib=memory))
assert len(states[0]) == 100 and states[0] == states[1]
assert vms[1]['quality_transitions'] == ['SM64_QUALITY frame=90 world=192x144 previous=256']
rom = Path('/path/to/local-input/Super Mario 64 (USA).z64')
assert hashlib.sha1(rom.read_bytes()).hexdigest() == '9bef1128717f958171a4afac3ed78ee2bb4e86ce'
assert rom.read_bytes() == (Path(builds['mips']['privateSource']) / 'baserom.us.z64').read_bytes()
report = dict(passed=True, package_sha256=package_info['package_sha256'], package_bytes=package.stat().st_size,
    binary_sha256=builds['mips']['binarySha256'], original_engine_files_unmodified=2500,
    host_fully_rendered_frames=host['fully_rendered_frames'], mips_fully_rendered_frames=6000,
    mips_game_states_identical_across_fixed_and_adaptive=True, vms=vms,
    original_rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(), private_only=True,
    physical_console_fps_measured=False, audible_hardware_audio_tested=False,
    world_header_sha256={name: hashlib.sha256((platform.parent / name).read_bytes()).hexdigest()
        for name in ('world_quality.h', 'world_scale.h')})
(Q / 'delivery.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
