#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Fetch only compiler pieces and requested public SF3000 headers, for QA builds.

SDK artifacts are NOT automatically approved for runtime redistribution.
No firmware driver is downloaded. Every fetched file is pinned and hashed.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
REV = '06eb2b99c4627a1200ecce188a80e029b60ed33e'
REPO = '700zx1/sf3000toolchain'
PREFIX = 'mipsel-buildroot-linux-gnu_sdk-buildroot/'
EXT = 'opt/ext-toolchain/'
SYS = 'mipsel-buildroot-linux-gnu/sysroot/'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('paths', nargs='*', help='Additional SDK-relative paths')
    p.add_argument('--initial', action='store_true')
    p.add_argument('--compile', action='store_true', help='Fetch only headers reported missing by the matching compiler')
    p.add_argument('--headers', action='store_true', help='Fetch bounded transitive standard-header references')
    args = p.parse_args()
    out = HERE / '_build/sf3000-sdk'
    out.mkdir(parents=True, exist_ok=True)
    tree_file = out / 'tree.json'
    if not tree_file.exists():
        tree_file.write_bytes(urllib.request.urlopen('https://api.github.com/repos/' + REPO + '/git/trees/' + REV + '?recursive=1').read())
    tree = json.loads(tree_file.read_text())
    assert not tree['truncated']
    entries = {x['path'][len(PREFIX):]: x for x in tree['tree'] if x['path'].startswith(PREFIX)}
    record_file = out / 'fetched.json'
    records = json.loads(record_file.read_text()) if record_file.exists() else {}
    def get(name):
        dest = out / name
        if dest.exists():
            return
        entry = entries[name]
        assert entry['type'] == 'blob' and entry.get('size', 0) < 32 * 1024 * 1024
        url = 'https://raw.githubusercontent.com/' + REPO + '/' + REV + '/' + PREFIX + name
        data = urllib.request.urlopen(url, timeout=90).read()
        assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == entry['sha']
        dest.parent.mkdir(parents=True, exist_ok=True)
        if entry['mode'] == '120000':
            target = data.decode()
            normalized = os.path.normpath(str(Path(name).parent / target))
            if normalized.startswith('..') or normalized.startswith('/'):
                raise ValueError('Out-of-SDK symlink')
            get(normalized)
            dest.symlink_to(target)
        else:
            dest.write_bytes(data)
            if entry['mode'] == '100755':
                dest.chmod(0o755)
        records[name] = dict(url=url, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), mode=entry['mode'])
        record_file.write_text(json.dumps(records, indent=2) + '\n')
        print('Fetched', name, len(data), flush=True)
    paths = list(args.paths)
    if args.initial:
        paths += [EXT + 'bin/mips-mti-linux-gnu-' + x for x in ('gcc', 'as', 'ld')]
        paths += [EXT + 'libexec/gcc/mips-mti-linux-gnu/6.3.0/' + x for x in ('cc1', 'collect2')]
        paths += [EXT + 'lib/gcc/mips-mti-linux-gnu/6.3.0/mipsel-r2-hard/lib/' + x for x in ('crtbegin.o', 'crtend.o', 'crtbeginS.o', 'crtendS.o', 'libgcc.a', 'libgcc_eh.a')]
        paths += [EXT + 'lib/gcc/mips-mti-linux-gnu/6.3.0/include/' + x for x in ('stddef.h', 'stdarg.h', 'stdbool.h', 'limits.h', 'syslimits.h', 'stdint.h') if EXT + 'lib/gcc/mips-mti-linux-gnu/6.3.0/include/' + x in entries]
        paths += [SYS + 'usr/lib/' + x for x in ('crt1.o', 'crti.o', 'crtn.o', 'libc.so', 'libc_nonshared.a', 'libSDL.so')]
        paths += [SYS + 'lib/' + x for x in ('ld.so.1', 'libc.so.6', 'libm.so', 'libm.so.6', 'libdl.so', 'libdl.so.2', 'libpthread.so', 'libpthread.so.0', 'librt.so.1', 'libgcc_s.so.1') if SYS + 'lib/' + x in entries]
    for name in paths:
        get(name)
    if args.headers:
        todo = ['stdint.h','limits.h','string.h','stdlib.h','fcntl.h','unistd.h','sys/mman.h','sys/stat.h','time.h','dlfcn.h','elf.h','errno.h','math.h','signal.h','stdarg.h','stdbool.h','stdio.h','SDL/SDL.h']
        seen = set()
        while todo and len(seen)<500:
            header = todo.pop()
            if header in seen:
                continue
            seen.add(header)
            name = SYS + 'usr/include/' + header
            if name not in entries:
                continue
            get(name)
            text = (out / name).read_text(errors='replace')
            for include in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)', text, re.M):
                relative = str(Path(header).parent / include)
                todo.append(relative if SYS+'usr/include/'+relative in entries else include)
        if todo:
            raise RuntimeError('Transitive headers exceeded 500 bounded names')
    if args.compile:
        command = ['python3', str(HERE / 'build.py'), '--target', 'glibc', '--sdk', str(out / SYS), '--cc', str(out / EXT / 'bin/mips-mti-linux-gnu-gcc')]
        for attempt in range(300):
            done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if done.returncode == 0:
                print(done.stdout)
                break
            match = re.search(r'fatal error: ([^:\n]+): No such file or directory', done.stderr)
            if not match:
                print(done.stdout, flush=True)
                print(done.stderr, flush=True)
                raise SystemExit(done.returncode)
            header = match.group(1)
            candidates = [SYS + 'usr/include/' + header, SYS + 'usr/include/SDL/' + header,
                          EXT + 'lib/gcc/mips-mti-linux-gnu/6.3.0/include/' + header,
                          EXT + 'lib/gcc/mips-mti-linux-gnu/6.3.0/include-fixed/' + header]
            selected = next((name for name in candidates if name in entries and not (out / name).exists()), None)
            if not selected:
                raise RuntimeError('No new matching public SDK header: ' + header + '\n' + done.stderr)
            get(selected)
        else:
            raise RuntimeError('Header acquisition exceeded 300 bounded attempts')
    print('Compiler:', out / EXT / 'bin/mips-mti-linux-gnu-gcc')
    print('Sysroot:', out / SYS)

if __name__ == '__main__':
    main()
