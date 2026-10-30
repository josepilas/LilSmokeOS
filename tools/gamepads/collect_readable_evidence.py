import json
from pathlib import Path
import shutil
P=Path(__file__).resolve().parents[2]
out=P/'test-results/sm64-readable-rk707-2026-10-11'
sources={
    'sm64-app':Path('/root/sm64-readable-final-app-20261011'),
    'renderer-sanitizers':Path('/root/sm64-readable-final-asan-20261011'),
    'usb-denied-fixture':Path('/root/usb-pad-9qt9dkv_'),
    'rk707-corrected-fixture':Path('/root/usb-pad-ebhtpx2b'),
    'rk707-live-ps':Path('/root/usb-pad-asgs4u1r'),
}
for name,source in sources.items():
    assert json.loads((source/'report.json').read_text())['passed']
    for file in source.rglob('*'):
        if not file.is_file() or not (file.suffix=='.png' or file.name in ('report.json','engine.log','renderer.log',
                'probe.log','driver.log','serial.log','memory.txt','usb-original.log','usb-portable.log')):continue
        target=out/name/file.relative_to(source);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(file,target)
print('Collected actual application, renderer and restricted USB evidence')
