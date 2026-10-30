"""Import the privately downloaded freeware test data; no game assets in releases."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import zipfile

P = Path(__file__).resolve().parents[3]
ROOT = Path('/root/lilsmoke-emulator-expansion-20261008')
B = Path('/root/lilsmoke-os/build/buildroot/output')
QA = ROOT / 'nx-qa'
QA.mkdir(exist_ok=True)
project = QA / 'original'
project.mkdir(exist_ok=True)
with zipfile.ZipFile(ROOT / 'engine-downloads/cavestoryen-private-test.zip') as archive:
    for info in archive.infolist():
        if info.is_dir(): continue
        relative = Path(info.filename)
        assert not relative.is_absolute() and '..' not in relative.parts
        target = project / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive.read(info))
exe = next(project.rglob('Doukutsu.exe'))
original_hashes = {str(p.relative_to(exe.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in exe.parent.rglob('*') if p.is_file()}
support = ROOT / 'built-native/data/share/nxengine/data'
support.mkdir(parents=True, exist_ok=True)
source = ROOT / 'sources/nxengine/data'
for path in source.iterdir():
    if path.is_file() and (path.suffix in ('.fnt', '.json', '.sif', '.dat') or
                          path.name.startswith('font_') or path.name == 'spot.png'):
        shutil.copyfile(path, support / path.name)
extractor = QA / 'nxextract-shim'
extract_command = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(B / 'staging'),
                   str(ROOT / 'native-candidates/bin/nxextract')]
extractor.write_text('#!/bin/sh\nexec ' + shlex.join(extract_command) + '\n')
extractor.chmod(0o700)
env = dict(os.environ, LILSMOKE_NX_SUPPORT=str(support), LILSMOKE_NX_CACHE=str(QA / 'cache'),
           LILSMOKE_NX_EXTRACTOR=str(extractor))
helper = ROOT / 'built-native/bin/lilsmoke-nx-data'
subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-g++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                str(P / 'package/lilsmoke-scripts/src/lilsmoke-nx-data.cpp'), '-lsodium', '-o', str(helper)], check=True)
command = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(B / 'staging'), str(helper)]
log = P / 'test-results/opendingux-completion-2026-10-09'
log.mkdir(parents=True, exist_ok=True)
with (log / 'nx-import.txt').open('w') as out:
    first = subprocess.run(command + [str(exe)], env=env, text=True, capture_output=True, timeout=120)
    out.write(first.stderr)
    assert first.returncode == 0, first.stderr
    data = Path(first.stdout.strip())
    assert (data / 'npc.tbl').is_file() and (data / 'sprites.sif').is_file()
    second = subprocess.run(command + [str(exe)], env=env, text=True, capture_output=True, check=True, timeout=30)
    assert second.stdout == first.stdout and 'importing' not in second.stderr
    assert subprocess.check_output(command + [str(data)], env=env, text=True).strip() == str(data)
    assert all(hashlib.sha256((exe.parent / name).read_bytes()).hexdigest() == digest
               for name, digest in original_hashes.items())
report = dict(passed=True, data=str(data), original_unchanged=True, cache_reused=True,
              original_file_count=len(original_hashes), game_data_bundled=False)
(log / 'nx-import.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
