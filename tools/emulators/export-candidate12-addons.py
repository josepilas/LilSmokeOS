"""Export selected generated build files to the host staging directory, not SD."""
import json
from pathlib import Path
import shutil

P = Path(__file__).resolve().parents[2]
ROOT = Path('/root/lilsmoke-emulator-expansion-20261008')
OUT = P / 'build/candidate12-addon-sources'

def copy(relative):
    source = ROOT / relative
    destination = OUT / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)

copy('optional-payload/cores-manifest.json')
manifest = json.loads((ROOT / 'optional-payload/cores-manifest.json').read_text())
cores = {'quicknes','snes9x_2002','gpsp','picodrive','prosystem','atari800','mednafen_wswan','prboom'}
for entry in manifest['cores']:
    filename = Path(entry['path']).name
    name = filename.removesuffix('_libretro.so')
    if name not in cores:
        continue
    copy('optional-payload/' + entry['path'])
    copy(f'optional-payload/emulators/info/{name}_libretro.info')
    for notice in (ROOT / 'optional-payload/emulators/licenses' / entry['core']).iterdir():
        if notice.is_file(): copy(str(notice.relative_to(ROOT)))
qualified = json.loads((P / 'tools/emulators/od-consoles/QUALIFICATION.json').read_text())
pins = json.loads((P / 'package/lilsmoke-od-consoles/sources.json').read_text())['engines']
for name in ('fceu320','gnuboy','prosystem','atari800','oswan'):
    assert qualified['engines'][name]['copyCandidate']
    copy(f'od-console-stage/storage/emulators/opendingux/{name}')
    for asset in qualified['engines'][name]['assets']['files']:
        copy(f'od-console-stage/storage/emulators/share/{name}/' + asset['path'])
    directory = ROOT / 'od-console-src' / pins[name].get('source_directory', name)
    for notice in directory.iterdir():
        if notice.is_file() and notice.name.lower().startswith(('copying','license','copyright','gpl')):
            copy(str(notice.relative_to(ROOT)))
for relative in ('built-native/stage-manifest.json', 'built-native/bin/prboom',
                 'built-native/data/share/prboom/prboom-plus.wad', 'built-native/licenses/prboom/COPYING',
                 'engine-sysroot/usr/lib/libSDL2_mixer-2.0.so.0', 'libretro/prboom/prboom.wad'):
    copy(relative)
print('Selected addon sources exported to ' + str(OUT))
