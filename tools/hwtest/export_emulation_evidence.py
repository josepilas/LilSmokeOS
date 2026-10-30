#!/usr/bin/env python3
"""Export measured public evidence, never ROMs, extracted assets or user saves."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PUBLIC = {'manifest.json','results.json','comparison.json','serial.log','jit-stats.txt',
          'input-events.json','manual-events.ndjson','recording-probe.json','recording.log','functional-checks.json',
          'core.log','software.log','dummy.log','software-alsa.log','software-alsa-input.log','software-alsa-ui.log'}

def inventory(folder):
    rows = []
    for path in sorted(folder.rglob('*')):
        if path.is_file() and path.name != 'evidence-index.json':
            rows.append(dict(path=path.relative_to(folder).as_posix(), bytes=path.stat().st_size,
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    (folder/'evidence-index.json').write_text(json.dumps(rows, indent=2))
    return len(rows)

def check_qa_archive(path):
    # Newc headers are fixed fields; examine every concatenated overlay too.
    data = gzip.decompress(path.read_bytes())
    offset, entries = 0, []
    while offset < len(data):
        while offset < len(data) and data[offset] == 0: offset += 1
        if offset == len(data): break
        if data[offset:offset+6] not in (b'070701', b'070702'):
            raise RuntimeError('invalid QA newc archive header')
        fields = [int(data[offset+6+i*8:offset+14+i*8],16) for i in range(13)]
        length, namesize = fields[6], fields[11]
        endname = offset+110+namesize
        if not namesize or endname > len(data) or data[endname-1] != 0:
            raise RuntimeError('invalid QA archive filename')
        name = data[offset+110:endname-1].decode('utf-8')
        payload = (endname+3)&~3
        offset = (payload+length+3)&~3
        if offset > len(data): raise RuntimeError('truncated QA archive payload')
        if name != 'TRAILER!!!': entries.append(name)
    forbidden = ('.sfc','.smc','.nes','.gb','.gbc','.gba','.mpq','.chd','.iso','.7z')
    found = [name for name in entries if name.lower().endswith(forbidden)]
    if found: raise RuntimeError(f'game/ROM assets must not be exported: {found}')
    return dict(entries_checked=len(entries), forbidden_game_assets=[],
                notice='Checked base newc archive and all appended overlays; not a complete release image.')

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--runs', type=Path, default=Path('/root/lilsmoke-performance/runs'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--kernel', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output/images/vmlinux'))
    p.add_argument('--refresh-index', action='store_true', help='rehash an existing evidence folder after adding the report')
    p.add_argument('--check-qa-image', action='store_true', help='check an existing exported QA image for embedded ROMs')
    a = p.parse_args()
    if a.refresh_index or a.check_qa_image:
        if not a.out.is_dir(): raise RuntimeError('existing evidence folder required')
        if a.check_qa_image:
            result = check_qa_archive(a.out/'qa-images/rootfs.cpio.gz')
            (a.out/'qa-images/archive-check.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(result), flush=True)
        print(f'EVIDENCE_INDEXED files={inventory(a.out)} destination={a.out}', flush=True)
        return
    a.out.mkdir(parents=True, exist_ok=False)
    names = ['control-stock-600','control-profile32-600','candidate-profile32-600',
             'final-comparison','final-row-comparison','core-final-selftest',
             'ui-manual-final','ui-final-verified','ui-final-row-validation']
    names.append('ui-final-vt-validation')
    names.append('video-presentation-isolation')
    names.append('failed-startup-metric-guard-fixed')
    for name in names:
        source = a.runs/name
        if not source.exists(): raise RuntimeError(f'missing required run: {source}')
        destination = a.out/name
        public_paths = []
        for directory, folders, filenames in os.walk(source):
            folders[:] = [f for f in folders if f not in ('storage','persist')]
            public_paths.extend(Path(directory)/f for f in filenames)
        for path in public_paths:
            # Do not traverse mounted storage/ROM caches, states, or binaries.
            relative = path.relative_to(source)
            if relative.parts[0] in ('storage','persist'): continue
            if path.name not in PUBLIC and path.suffix not in ('.png','.ppm','.webm'): continue
            target = destination/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix == '.ppm':
                subprocess.run(['convert',str(path),str(target.with_suffix('.png'))],check=True)
            else:
                shutil.copyfile(path,target)
        for log in ('boot-state.log','vt-state.log','logs/lilsmoke-ui.log','logs/retroarch.log'):
            path = source/'persist'/log
            if path.exists():
                target = destination/'guest-logs'/Path(log).name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path,target)
    for directory in ('remote-input','emulation-performance/pinned-cores'):
        source = ROOT/'test-results'/directory
        for path in source.glob('*.log') if directory == 'remote-input' else source.glob('*.build-info'):
            target = a.out/'regressions'/path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path,target)
    for name in ('baseline-safe.log','candidate.log','baseline-safe-bench.log','candidate-bench.log'):
        path = Path('/root/lilsmoke-performance/stretch-smoke')/name
        if path.exists():
            target = a.out/'regressions'/('stretch-'+name)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,target)
    checks = ROOT/'test-results/emulation-performance/final-checks'
    if not (checks/'results.json').is_file():
        raise RuntimeError('run final_emulation_checks.py before exporting')
    for path in checks.iterdir():
        if path.suffix in ('.json', '.log'):
            target = a.out/'regressions'/path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    qa = a.out/'qa-images'
    qa.mkdir()
    shutil.copyfile(a.kernel,qa/'vmlinux')
    shutil.copyfile(a.runs/'ui-final-vt-validation/ui-test.cpio.gz',qa/'rootfs.cpio.gz')
    (qa/'archive-check.json').write_text(json.dumps(check_qa_archive(qa/'rootfs.cpio.gz'), indent=2))
    print(f'EVIDENCE_EXPORTED files={inventory(a.out)} destination={a.out}',flush=True)

if __name__ == '__main__': main()
