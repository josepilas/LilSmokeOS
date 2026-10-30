"""Collect small QA evidence only; never publish private ROMs or engine binaries."""
import json
from pathlib import Path
import shutil

P=Path(__file__).resolve().parents[2]
out=P/'test-results/pcsx4all-app-2026-10-11'
sources={'pcsx-vm':Path('/root/pcsx-app-qa-20261011-retry4'),
         'sm64-original-vm':Path('/root/lilsmoke-sm64-original-vm-20261011-faithful')}
duplicates=sorted(Path('/root/lilsmoke-native-duplicates-20261010').glob('frontend-*/report.json'),key=lambda p:p.stat().st_mtime)
if duplicates:sources['retroarch-duplicates-vm']=duplicates[-1].parent
for name,source in sources.items():
    report=json.loads((source/'report.json').read_text())
    assert report['passed'],source
    destination=out/name;destination.mkdir(parents=True,exist_ok=True)
    selected=['report.json','memory.txt','serial.log','engine.log','pcsx4all.log','retroarch.log']
    selected += [p.name for p in source.glob('*.png')]
    for file in selected:
        if (source/file).is_file():shutil.copyfile(source/file,destination/file)
    print('Collected',name,'small evidence files only')
