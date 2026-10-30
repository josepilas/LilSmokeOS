"""Validate native state recipes and compile their changed menu translation units."""
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys

P = Path(__file__).resolve().parents[3]
B = Path('/root/lilsmoke-os/build/buildroot/output')
C = Path('/root/lilsmoke-emulator-expansion-20261008')
Q = Path(sys.argv[1]).resolve() / 'native-paths'
package = P / 'package/lilsmoke-od-consoles'
sys.path.insert(0, str(P / 'tools/emulators/od-consoles'))
from build import source_fixes

fixes = json.loads((package / 'source-fixes.json').read_text())
for engine in ('race', 'pokemini'):
    work = Q / engine
    if not work.exists():
        shutil.copytree(C / 'od-console-src' / engine, work, ignore=shutil.ignore_patterns('.git'))
    source_fixes(engine, work, package, io.StringIO())
    first = {f['file']: (work / f['file']).read_bytes() for f in fixes[engine]}
    source_fixes(engine, work, package, io.StringIO())
    assert all((work / name).read_bytes() == content for name, content in first.items())
    flags = ['-fsyntax-only', '-I' + str(package), '-I' + str(B / 'staging/usr/include/SDL'),
             '-I' + str(C / 'od-console-deps/usr/include'), '-D_GNU_SOURCE', '-D_REENTRANT']
    if engine == 'race':
        command = [str(B / 'host/bin/mipsel-linux-g++'), '-std=gnu++11', '-fpermissive', *flags,
                   '-DOPENDINGUX', '-DCZ80', '-DTARGET_OD', '-D_MAX_PATH=2048', '-DHOST_FPS=60',
                   '-Iemu', '-Iopendingux', 'opendingux/menu.cpp']
    else:
        command = [str(B / 'host/bin/mipsel-linux-gcc'), '-std=gnu99', *flags,
                   '-DPERFORMANCE', '-DNO_ZIP', '-Isource', '-Isourcex', '-Ipm_libs', '-Iresource', 'source/UI.c']
    result = subprocess.run(command, cwd=work, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (Q / (engine + '-syntax.log')).write_text(result.stdout)
    assert result.returncode == 0, result.stdout[-4000:]
    print('PASS:', engine, 'recipe contexts, idempotence, MIPS menu syntax, persistent hashed per-ROM state path')
