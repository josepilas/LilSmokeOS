#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Retain a private completed native build for additional resume/VM QA.

Pauses only the verified isolated make process group while copying generated
work. Never writes the SD or a distribution tree. The captured ELF must later
match the independently committed native-QA game before it may be used.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import signal
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = args.native.resolve()
    stage = root / 'Test App/.sm64-cache/working-imported'
    build = stage / 'source/build/original-mips'
    name = 'sm64.us.f3dex2e'
    args.out.mkdir(parents=True, exist_ok=False)
    libc = ctypes.CDLL(None, use_errno=True)
    deadline = time.monotonic() + 3600
    while not build.is_dir():
        if time.monotonic() > deadline: raise TimeoutError('Native build directory did not appear')
        time.sleep(1)
    fd = libc.inotify_init1(os.O_CLOEXEC)
    if fd < 0: raise OSError(ctypes.get_errno(), 'inotify_init1')
    group = None
    try:
        if libc.inotify_add_watch(fd, os.fsencode(build), 0x8 | 0x80) < 0:
            raise OSError(ctypes.get_errno(), 'inotify_add_watch')
        found = False
        while not found:
            if time.monotonic() > deadline: raise TimeoutError('Private build capture timed out')
            if not select.select([fd], [], [], 2)[0]: continue
            data = os.read(fd, 65536)
            offset = 0
            while offset < len(data):
                _, mask, _, size = struct.unpack_from('iIII', data, offset)
                entry = data[offset + 16:offset + 16 + size].split(b'\0', 1)[0]
                if entry == name.encode() and mask & (0x8 | 0x80): found = True
                offset += 16 + size
        for proc in Path('/proc').iterdir():
            if not proc.name.isdecimal(): continue
            try:
                command = (proc / 'cmdline').read_bytes()
                if (b'compiler/usr/bin/make' not in command or
                    (proc / 'cwd').resolve() != stage / 'source'): continue
                pid = int(proc.name)
                if os.getpgid(pid) == pid and os.getsid(pid) == pid:
                    group = pid
                    break
            except (OSError, ProcessLookupError):
                continue
        if group is None: raise RuntimeError('No isolated native make group matched')
        os.killpg(group, signal.SIGSTOP)
        # All producers in this serial make group are stopped. The supervising
        # Python remains live but cannot commit/prune until make has exited.
        target = args.out / 'PRIVATE-stage'
        shutil.copytree(stage, target)
        binary = target / 'source/build/original-mips' / name
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        report = dict(captured=True, native=str(root), stage=str(target),
            generated_elf_sha256=digest, make_group=group,
            private_game_data=True, requires_native_commit_hash_match=True)
        (args.out / 'capture.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2), flush=True)
    finally:
        if group is not None:
            try: os.killpg(group, signal.SIGCONT)
            except ProcessLookupError: pass
        os.close(fd)


if __name__ == '__main__': main()
