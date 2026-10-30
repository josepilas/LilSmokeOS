#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Serial isolated runtime build. SDKs are read-only; no apt or SDK install."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
GAMEPAD = HERE / 'gamepad-state.h'

def build(args):
    if args.sdk: args.sdk=args.sdk.resolve(strict=True)
    if args.cc and args.target=='glibc': args.cc=str(Path(args.cc).resolve(strict=True))
    out = (args.out or HERE / '_build' / args.target).resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
             '-I' + str(HERE), '-I' + str(GAMEPAD.parent)]
    if args.target == 'host':
        cc = args.cc or 'cc'
        abi = 'host-glibc'
        includes = subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True).split()
        libraries = subprocess.check_output(['pkg-config', '--libs', 'sdl2'], text=True).split()
    elif args.target == 'musl':
        if not args.sdk:
            raise SystemExit('musl requires --sdk BUILDROOT_OUTPUT (external SDK, never bundled)')
        sdk = args.sdk
        cc = args.cc or str(sdk / 'host/bin/mipsel-buildroot-linux-musl-gcc')
        abi = 'mipsel-o32-hardfloat-musl'
        flags += ['-mips32r2', '-mabi=32', '-mfp32', '-mhard-float', '-Wno-error=unused-variable']
        includes = ['-I' + str(sdk / 'staging/usr/include/SDL2')]
        libraries = ['-lSDL2']
    else:
        if not args.sdk or not args.cc:
            raise SystemExit('glibc requires --sdk SYSROOT --cc PUBLIC_SF3000_GCC (no musl fallback)')
        cc = args.cc
        abi = 'mipsel-o32-hardfloat-glibc'
        tool_dir = out / '_tools'
        tool_dir.mkdir(exist_ok=True)
        for tool in ('as', 'ld'):
            link = tool_dir / tool
            if not link.exists():
                link.symlink_to(str(Path(cc).parent / ('mips-mti-linux-gnu-' + tool)))
        # Relocate the public sysroot's absolute-path linker script without
        # changing the SDK or any runtime ELF. No static libc is used.
        (tool_dir / 'libc.so').write_text('GROUP ( ' + str(args.sdk / 'lib/libc.so.6') + ' ' +
            str(args.sdk / 'usr/lib/libc_nonshared.a') + ' AS_NEEDED ( ' + str(args.sdk / 'lib/ld.so.1') + ' ) )\n')
        flags += ['-fno-use-linker-plugin', '-fno-lto', '-B' + str(tool_dir) + '/', '-B' + str(args.sdk / 'usr/lib') + '/',
                  '--sysroot=' + str(args.sdk), '-Wl,--sysroot=' + str(args.sdk),
                  '-Wl,-rpath-link,' + str(args.sdk / 'lib'), '-Wl,-rpath-link,' + str(args.sdk / 'usr/lib'),
                  '-idirafter', str(args.sdk / 'usr/include'), '-EL', '-mips32r2', '-mabi=32', '-mfp32', '-mhard-float', '-static-libgcc',
                  '-L' + str(tool_dir), '-L' + str(args.sdk / 'lib'), '-L' + str(args.sdk / 'usr/lib')]
        includes = ['-I' + str(args.sdk / 'usr/include/SDL')]
        libraries = ['-lSDL']
    flags += ['-DLILRETRO_ABI="' + abi + '"', *includes]
    inputs = [HERE / 'lilretro.c', HERE / 'tests/test_core.c', HERE / 'tests/test_driver.c', HERE / 'vendor/libretro.h',
              HERE / 'vendor/jsmn.h', GAMEPAD]
    hashes = {p.relative_to(HERE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    command = [cc, *flags, str(HERE / 'lilretro.c'), '-o', str(out / 'lilretro'), *libraries, '-ldl', '-lm']
    print(shlex.join(command), flush=True)
    subprocess.run(command, check=True)
    subprocess.run([cc, *flags, '-fPIC', '-shared', str(HERE / 'tests/test_core.c'), '-o', str(out / 'test_core.so')], check=True)
    subprocess.run([cc, *flags, '-fPIC', '-shared', str(HERE / 'tests/test_driver.c'), '-o', str(out / 'test_driver.so')], check=True)
    if hashes != {p.relative_to(HERE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:
        raise RuntimeError('Build inputs changed during compilation')
    (out / 'profile.json').write_text(json.dumps(dict(schema=1, abi=abi, max_width=1024, max_height=768,
       max_audio_frames=8192, audio_latency_ms=64, controller_port=0, system_directory='system', save_directory='saves', options={}), indent=2) + '\n')
    report = dict(abi=abi, compiler=cc, command=command, sources=hashes,
                  binaries={p.name:dict(sha256=hashlib.sha256(p.read_bytes()).hexdigest(), bytes=p.stat().st_size)
                            for p in (out / 'lilretro', out / 'test_core.so')})
    (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['binaries']), flush=True)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', choices=['host', 'musl', 'glibc'], default='host')
    p.add_argument('--out', type=Path)
    p.add_argument('--sdk', type=Path)
    p.add_argument('--cc')
    build(p.parse_args())
