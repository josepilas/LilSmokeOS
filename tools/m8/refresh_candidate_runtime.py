#!/usr/bin/env python3
"""Refresh generated candidate files only; never opens a physical device."""
import argparse
import json
from pathlib import Path
import shutil
from build_stock_candidate import BOARD, build_display_adapter, prepare_runtime, run, sha
from stock_format import inspect

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--image', type=Path, required=True)
p.add_argument('--payload', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--full-runtime', action='store_true', help='populate a new payload files/manifest from the target tree')
p.add_argument('--sync-all', action='store_true', help='synchronize the complete existing generated payload into the candidate')
p.add_argument('--runtime-path', action='append', default=[], help='refresh a specific existing runtime object instead of the default power/PocketSNES objects')
a = p.parse_args()
report_path = a.image.with_suffix('.img.json')
report = json.loads(report_path.read_text())
if not a.image.is_file() or sha(a.image) != report['image_sha256']:
    p.error('candidate does not match its generated build report')
layout = inspect(a.image)
if [v['label'] for v in layout['partitions']] != ['LILOS', 'LILROMS']:
    p.error('not a two-partition generated candidate')
if a.full_runtime:
    if (a.payload / 'files').exists() or (a.payload / 'runtime.tsv').exists():
        p.error('full refresh requires a new payload without files or runtime.tsv')
    report['runtime'] = prepare_runtime(a.build / 'target', a.payload)
rows = [line.split('\t') for line in (a.payload / 'runtime.tsv').read_text().splitlines()]
objects = {r[2]: r[3] for r in rows if r[0] == 'F'}
updated = []
for path in (() if a.full_runtime else (a.runtime_path or ('usr/lib/lilsmoke/lilsmoke-power', 'usr/lib/lilsmoke/od-launch',
             'usr/lib/lilsmoke/od-emulators/pocketsnes'))):
    name = objects[path]
    if len(name) != 10 or not name[:6].isdigit() or not name.endswith('.bin'):
        p.error('unexpected object name')
    shutil.copyfile(a.build / 'target' / path, a.payload / 'files' / name)
    updated.append('files/' + name)
if a.full_runtime or a.sync_all:
    updated = ['runtime.tsv'] + ['files/' + path.name for path in (a.payload / 'files').iterdir()]
shutil.copyfile(BOARD / 'm8-launch.sh', a.payload / 'm8-launch.sh')
build_display_adapter(a.build, a.payload / 'libm8-display.so')
report['runtime']['regular_bytes'] = sum(f.stat().st_size for f in (a.payload / 'files').iterdir())
report['runtime']['records'] = len(rows)
report['runtime']['objects'] = len(list((a.payload / 'files').iterdir()))
report['output'] = str(a.image.resolve())
report['display_adapter'] = 'opt-in native RGB565 hcfb presenter and hardware scaler; guarded software fallback; physical verification pending'
report['audio'] = 'isolated native PCM with bounded socket/pipe transports and stage diagnostics; audible physical verification pending'
report['shutdown'] = 'terminal static RAM guard pauses init, stops users, releases SD mounts, flushes devices, checks FAT clean flags, displays success for 10 seconds then black and kernel halt; physical test pending'
embedded = {k: v for k, v in report.items() if k not in ('image_sha256', 'output', 'partitions')}
(a.payload / 'candidate.json').write_text(json.dumps(embedded, indent=2) + '\n')
offset = f'{a.image}@@1048576'
for name in ['m8-launch.sh', 'libm8-display.so', 'candidate.json'] + updated:
    run(['mcopy', '-o', '-i', offset, a.payload / name, '::/lilsmoke/' + name])
report['image_sha256'] = sha(a.image)
report_path.write_text(json.dumps(report, indent=2) + '\n')
a.image.with_suffix('.img.sha256').write_text(report['image_sha256'] + '  ' + a.image.name + '\n')
print('Generated candidate refreshed: ' + report['image_sha256'])
