"""Persist only logs/reports/screenshots; never ROMs or private game binaries."""
import json
from pathlib import Path
import shutil
P=Path(__file__).resolve().parents[2]
out=P/'test-results/sm64-local-usb-gamepads-2026-10-11'
usb=sorted(Path('/root').glob('usb-pad-*/report.json'),key=lambda f:f.stat().st_mtime)[-1].parent
sources={'sm64-local-app':Path('/root/sm64-local-app-20261011'),
         'pcsx-regression':Path('/root/pcsx-app-qa-20261011-c25'),'usb-driver':usb}
for name,source in sources.items():
    assert json.loads((source/'report.json').read_text())['passed']
    dest=out/name;dest.mkdir(parents=True,exist_ok=True)
    files=['report.json','memory.txt','serial.log','engine.log','pcsx4all.log','ui.log','driver.log','probe.log']
    files+=[f.name for f in source.glob('*.png')]
    for file in files:
        if (source/file).is_file():shutil.copyfile(source/file,dest/file)
    print('Collected',name)
