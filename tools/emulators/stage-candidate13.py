"""Export only production MIPS artifacts and their complete library closure."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import argparse

P=Path(__file__).resolve().parents[2]
R=Path('/root/lilsmoke-emulator-expansion-20261008')
B=Path('/root/lilsmoke-os/build/buildroot/output')
S=R/'engine-sysroot'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--generation',choices=['candidate13','candidate14','candidate15'],default='candidate13')
generation=parser.parse_args().generation
OUT=P/('build/'+generation+'-addons')
SD=OUT/'rom-partition'
READELF=B/'host/bin/mipsel-buildroot-linux-musl-readelf'
files={}
pending=[]

def copy(source,relative):
    source=Path(source).resolve()
    if not source.is_file() or '-qa' in str(source) or 'dummy' in str(source):
        raise RuntimeError('Missing or QA release artifact: '+str(source))
    destination=SD/relative
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,destination)
    files[relative]=dict(bytes=destination.stat().st_size,sha256=hashlib.sha256(destination.read_bytes()).hexdigest())

def elf(source,relative):
    text=subprocess.check_output([str(READELF),'-h','-A','-d',str(source)],text=True)
    assert 'ELF32' in text and 'little endian' in text and 'MIPS' in text
    assert not re.search(r'NEEDED.*\[(?:libGL|libGLES|libvulkan|libX11)',text),relative
    copy(source,relative)
    pending.extend(re.findall(r'NEEDED.*\[([^\]]+)\]',text))

def tree(source,destination):
    for item in sorted(source.rglob('*')):
        if item.is_file():copy(item,str(Path(destination)/item.relative_to(source)))

assert json.loads((P/'test-results/opendingux-completion-2026-10-09/nx-game/result.json').read_text())['passed']
assert json.loads((P/'test-results/opendingux-completion-2026-10-09/mgba-game/result.json').read_text())['passed']
assert json.loads((P/'test-results/opendingux-completion-2026-10-09/pico-game/result.json').read_text())['passed']
legacy=json.loads((P/'test-results/opendingux-completion-2026-10-09/legacy-menus/result.json').read_text())
assert legacy['passed'] and set(legacy['engines'])=={'openbor','mame4all'}

console=['picodrive','temper','handy','stella','race','pokemini','fceu320','gnuboy','prosystem','atari800','oswan']
if generation=='candidate15':
    regba=json.loads((P/'test-results/opendingux-pico-gba-2026-10-09/regba-game/result.json').read_text())
    assert regba['passed'] and regba['binary_sha256']==hashlib.sha256((R/'od-console-stage/storage/emulators/opendingux/regba').read_bytes()).hexdigest()
    console.append('regba')
engines=['mgba','easyrpg','scummvm','prboom']
candidates=['nxengine','nxextract','openbor','mame4all','mame4all-engine']
for name in console:
    elf(R/'od-console-stage/storage/emulators/opendingux'/name,'emulators/opendingux/'+name)
    tree(R/'od-console-stage/storage/emulators/share'/name,'emulators/share/'+name)
for name in engines:
    elf(R/'built-native/bin'/name,'emulators/opendingux/'+name)
for name in candidates:
    source=R/'built-native/bin'/name
    if not source.is_file():source=R/'native-candidates/bin'/name
    elf(source,'emulators/opendingux/'+name)
tree(R/'native-candidates/data/share/mame4all','emulators/opendingux/data/share/mame4all')
tree(R/'built-native/data/share/nxengine','emulators/opendingux/data/share/nxengine')
tree(R/'built-native/data/share/prboom','emulators/opendingux/data/share/prboom')
tree(R/'built-native/licenses','emulators/licenses/native-engines')
tree(R/'native-candidates/licenses','emulators/licenses/native-candidates')

pins=json.loads((P/'package/lilsmoke-od-consoles/sources.json').read_text())['engines']
for name in console:
    source=R/'od-console-src'/pins[name].get('source_directory',name)
    notices=[item for item in source.iterdir() if item.is_file() and item.name.lower().startswith(('license','copying','copyright','gpl'))]
    if not notices:
        if name=='regba':notices=[source/'doc/copyright_en.txt']
    if not notices:
        # Temper's repository has no standalone license file. Retain its own
        # documentation; do not invent a license or grant redistribution rights.
        assert name=='temper','No license: '+name
        notices=[source/'README.md']
    for notice in notices:copy(notice,'emulators/licenses/native-'+name+'/'+notice.name)

# Engine-owned support tables and UI assets, never commercial game data.
scumm=R/'sources/scummvm'
aux=['kyra.dat','lure.dat','drascula.dat','queen.tbl','sky.cpt','fonts.dat','macgui.dat','encoding.dat','classicmacfonts.dat']
themes=['scummclassic.zip','gui-icons.dat','translations.dat']
for name in aux:
    source=scumm/'dists/engine-data'/name
    copy(source,'emulators/opendingux/data/share/scummvm/'+name)
    copy(source,'bios/scummvm/extra/'+name)
for name in themes:
    source=scumm/'gui/themes'/name
    copy(source,'emulators/opendingux/data/share/scummvm/'+name)
    copy(source,'bios/scummvm/theme/'+name)

for core in sorted((R/'built-libretro').glob('*_libretro.so')):
    elf(core,'emulators/libretro/'+core.name)
    info=R/'optional-payload/emulators/info'/core.with_suffix('.info').name
    if info.is_file():copy(info,'emulators/info/'+info.name)
    else:
        assert core.stem in ('easyrpg_libretro','scummvm_libretro')
        info=OUT/(core.stem+'.info')
        info.parent.mkdir(parents=True,exist_ok=True)
        name='EasyRPG' if core.stem.startswith('easyrpg') else 'ScummVM (11 Classic 2D Engines)'
        extension='ldb' if core.stem.startswith('easyrpg') else 'scummvm'
        info.write_text('display_name = "'+name+'"\ncorename = "'+name+'"\n'+
            'supported_extensions = "'+extension+'"\nsupports_no_game = "true"\n',encoding='ascii')
        copy(info,'emulators/info/'+info.name)
    native=R/'sources'/('easyrpg' if core.stem.startswith('easyrpg') else 'scummvm-libretro-2.9')
    if core.stem in ('easyrpg_libretro','scummvm_libretro'):
        copy(native/'COPYING','emulators/licenses/'+core.stem+'/COPYING')
    else:
        lock=json.loads((R/'optional-payload/cores-manifest.json').read_text())
        entry=next(x for x in lock['cores'] if Path(x['path']).name==core.name)
        tree(R/'optional-payload/emulators/licenses'/entry['core'],'emulators/licenses/libretro-'+entry['core'])

libraries={}
while pending:
    name=pending.pop()
    if name in libraries:continue
    assert '/' not in name and 'qa' not in name.lower()
    candidates=[R/'built-native/lib'/name,S/'usr/lib'/name,S/'lib'/name]
    source=next((item.resolve() for item in candidates if item.is_file()),None)
    assert source,'Missing dependency: '+name
    libraries[name]=str(source)
    base=B/'target/usr/lib'/name
    if not base.is_file():base=B/'target/lib'/name
    same=base.is_file() and hashlib.sha256(base.read_bytes()).digest()==hashlib.sha256(source.read_bytes()).digest()
    if not same:elf(source,'emulators/lib/'+name)
    else:
        pending.extend(re.findall(r'NEEDED.*\[([^\]]+)\]',subprocess.check_output([str(READELF),'-d',str(source)],text=True)))

for info in (SD/'emulators/info').iterdir():
    target=B/'target/usr/share/libretro/info'/info.name
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(info,target)
    old_generated=B/'target/usr/share/retroarch/info'/info.name
    if old_generated.is_file() and old_generated.read_bytes()==info.read_bytes():old_generated.unlink()
manifest=dict(generation=generation,files=files,native_apps=console+engines+['nxengine','openbor','mame4all'],
    libraries=libraries,hardware_verified=False,game_data_bundled=False,
    limitations=['N64 has no validated software-only native alternative','OpenBOR is legacy 32-bit r4034',
                 'MAME4ALL requires matching 0.37b5 ROM sets; noncommercial upstream license',
                 'Temper upstream lacks standalone license text; private build, redistribution not cleared',
                 'Only NXEngine, mGBA and PicoDrive have new gameplay smoke tests; other ports menu-tested'])
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Production SD payload:',len(files),'files,',sum(v['bytes'] for v in files.values()),'bytes')
