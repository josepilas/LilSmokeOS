"""Incrementally rebuild two staged optional native engines without SDK installs."""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
C = Path('/root/lilsmoke-emulator-expansion-20261008')
Q = Path(sys.argv[1]).resolve()
assert str(Q).startswith('/root/lilsmoke-state-qa-'), Q
sys.path.insert(0, str(P / 'tools/emulators/od-consoles'))
from build import FLAGS, source_fixes

package = P / 'package/lilsmoke-od-consoles'
manifest = json.loads((package / 'sources.json').read_text())['engines']
sysroot = B / 'host/mipsel-buildroot-linux-musl/sysroot'
cc, cxx = (str(B / ('host/bin/mipsel-linux-' + name)) for name in ('gcc', 'g++'))
reports = []
for engine in ('race', 'pokemini'):
    entry = manifest[engine]
    cached = C / 'od-console-build' / (engine + '-' + entry['revision'][:12])
    work = Q / 'native-builds' / engine
    if not work.exists():
        shutil.copytree(cached, work, ignore=shutil.ignore_patterns('.git'))
    before = {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest() for p in work.rglob('*.o')}
    with (Q / (engine + '-build.log')).open('w') as log:
        patchlog = io.StringIO()
        source_fixes(engine, work, package, patchlog)
        log.write(patchlog.getvalue())
        log.flush()
        native_flags = FLAGS + ' -I' + str(package) + ' -I' + str(C / 'od-console-deps/usr/include')
        command = ['make', '--trace', '-j1', '-f', str(package / ('Makefile.' + engine)),
                   'CC=' + cc, 'CXX=' + cxx, 'LD=' + cc,
                   'SDL_CFLAGS=-I' + str(sysroot / 'usr/include/SDL'), 'SDL_LIBS=-lSDL -pthread',
                   'OD_FLAGS=' + native_flags, 'OD_SYSROOT=' + str(sysroot),
                   'OD_PACKAGE=' + str(package), 'OD_DEPS=' + str(C / 'od-console-deps')]
        cwd = work / entry.get('make_directory', '.')
        log.write('\nCOMMAND ' + repr(command) + '\n')
        log.flush()
        subprocess.run(command, cwd=cwd, check=True, stdout=log, stderr=subprocess.STDOUT)
    target = Q / 'native-stage/storage/emulators/opendingux' / engine
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(work / entry['binary'], target)
    subprocess.run([str(B / 'host/bin/mipsel-linux-strip'), str(target)], check=True)
    data = target.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01' and int.from_bytes(data[18:20], 'little') == 8
    dynamic = subprocess.check_output([str(B / 'host/bin/mipsel-linux-readelf'), '-d', str(target)], text=True)
    (Q / (engine + '-dynamic.txt')).write_text(dynamic)
    assert b'/states' in data and b'.slot%d.state' in data, engine
    after = {str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest() for p in work.rglob('*.o')}
    changed = [name for name, digest in after.items() if before.get(name) != digest]
    report = dict(engine=engine, path=str(target), install_path='/storage/emulators/opendingux/' + engine,
                  sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), source_commit=entry['revision'],
                  changed_objects=changed, reused_objects=sum(before.get(n) == d for n, d in after.items()),
                  shared_sdk_written=False, sd_card_written=False)
    reports.append(report)
    print(json.dumps(report), flush=True)
(Q / 'native-build-report.json').write_text(json.dumps(reports, indent=2) + '\n')
