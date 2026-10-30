"""Collect small evidence only; omit ROMs, extracted assets and private binaries."""
import json
from pathlib import Path
import shutil

P = Path(__file__).resolve().parents[2]
out = P/'test-results/pcsx4all-launch-sm64-2026-10-11'
sources = {'pcsx-vm': Path('/root/pcsx-app-qa-20261011-c24ui'),
           'sm64-original-vm': Path('/root/sm64-original-adapters-20261011-final2')}
for name, source in sources.items():
    report = json.loads((source/'report.json').read_text())
    assert report['passed']
    destination = out/name
    destination.mkdir(parents=True, exist_ok=True)
    selected = ['report.json', 'memory.txt', 'serial.log', 'engine.log', 'pcsx4all.log', 'ui.log']
    selected += [file.name for file in source.glob('*.png')]
    for file in selected:
        if (source/file).is_file():
            shutil.copyfile(source/file, destination/file)
    print('Collected', name, 'small evidence files only')
