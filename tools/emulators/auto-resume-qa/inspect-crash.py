# SPDX-License-Identifier: MPL-2.0
"""Read a private MIPS core with text-matched installed/build symbols only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

B = Path('/root/lilsmoke-os/build/buildroot/output')
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--input', type=Path, required=True)
ap.add_argument('--core', type=Path, help='Defaults to the only private core.* payload in --input')
ap.add_argument('--out', type=Path, required=True)
a = ap.parse_args()
if a.core is None:
    candidates = [p for p in a.input.glob('core.*') if p.is_file() and not p.name.endswith('.txt')]
    assert len(candidates) == 1, 'Select a single core payload explicitly'
    a.core = candidates[0]
a.out.mkdir(exist_ok=False)
symbols = a.out / 'symbols'
symbols.mkdir()


def text_hash(path):
    objcopy = B / 'host/bin/mipsel-buildroot-linux-musl-objcopy'
    text = subprocess.run([str(objcopy), '-O', 'binary', '-j', '.text', str(path), '/dev/stdout'],
                          check=True, capture_output=True, timeout=15).stdout
    return hashlib.sha256(text).hexdigest()


matches = {}
for installed, built, alias in (
    (B / 'target/lib/ld-musl-mipsel.so.1', B / 'build/musl-1.2.4/lib/libc.so', 'ld-musl-mipsel.so.1'),
    (B / 'target/usr/lib/libSDL2-2.0.so.0', B / 'build/sdl2-2.28.5/build/.libs/libSDL2-2.0.so.0', 'libSDL2-2.0.so.0'),
    (B / 'target/usr/bin/retroarch', B / 'build/retroarch-mips-v1.19.1/retroarch', 'retroarch')):
    expected, actual = text_hash(installed), text_hash(built)
    assert expected == actual, ('Symbol .text does not match installed binary', str(built))
    (symbols / alias).symlink_to(built)
    matches[alias] = dict(installed=str(installed), symbols=str(built), text_sha256=actual)
(a.out / 'symbol-matches.json').write_text(json.dumps(matches, indent=2) + '\n')
search = ':'.join(str(p) for p in (symbols, a.input, B / 'target/usr/lib', B / 'target/lib',
    B / 'target/usr/lib/libretro', B / 'target/usr/lib/directfb-1.7-7/systems',
    B / 'target/usr/lib/directfb-1.7-7/inputdrivers', B / 'target/usr/lib/directfb-1.7-7/wm'))
commands = ['set pagination off', 'set debuginfod enabled off', 'set print frame-arguments all',
    'set heuristic-fence-post 100000', 'set sysroot ' + str(a.out / 'no-sysroot'),
    'set solib-search-path ' + search, 'info symbol $pc', 'info sharedlibrary',
    'thread apply all bt 30', 'info registers', 'x/12i $pc-16', 'x/96a $sp',
    'x/10wx $s1', 'x/24wx $s2', 'info proc mappings']
command = ['taskset', '-c', str(min(os.sched_getaffinity(0))), 'nice', '-n', '10',
           'prlimit', '--as=805306368', '--', 'gdb-multiarch', '-nx', '-batch']
for instruction in commands:
    command += ['-ex', instruction]
command += [str(symbols / 'retroarch'), str(a.core)]
(a.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
(a.out / 'backtrace.txt').write_text(result.stdout)
print(result.stdout[-13000:])
