#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Serial audio probe build, CPU3/nice15/768MiB. No downloads or private inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

from stage import HERE, stage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('host', 'mips'), default='mips')
    parser.add_argument('--upstream', type=Path, default=HERE.parent / '_build/upstream')
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    sdk = Path(os.environ.get('SDK', '/root/lilsmoke-os/build/buildroot/output'))
    out = HERE / '_build' / args.target
    out.mkdir(parents=True, exist_ok=True)
    staged = stage(upstream, HERE / '_build/stage')
    cc = str(sdk / 'host/bin/mipsel-buildroot-linux-musl-gcc') if args.target == 'mips' else 'gcc'
    flags = ['-std=gnu2x', '-O2', '-fno-strict-aliasing', '-fwrapv', '-ffunction-sections',
             '-fdata-sections', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE',
             '-DENABLE_WAD16', '-DDISABLE_WAD14', '-DDISABLE_WAD17', '-DFROGGY_SDL_AUDIO=1']
    includes = [staged, HERE] + [upstream / p for p in (
        'src', 'src/image', 'vendor/miniaudio', 'vendor/stb/vorbis', 'vendor/stb/ds',
        'vendor/stb/image', 'vendor/bzip2', 'vendor/miniz')]
    libs = ['-lm', '-lpthread', '-ldl']
    if args.target == 'mips':
        flags += ['-mips32r2', '-mabi=32', '-mfp32', '-mhard-float']
        includes += [sdk / 'staging/usr/include/SDL2']
        libs += ['-lSDL2']
        libs += ['-latomic']
    else:
        flags += shlex.split(subprocess.check_output(['sdl2-config', '--cflags'], text=True))
        libs += shlex.split(subprocess.check_output(['sdl2-config', '--libs'], text=True))
    flags += ['-I' + str(p) for p in includes]
    sources = [staged / 'ma_audio_system.c', HERE / 'probe.c'] + [upstream / p for p in (
        'src/data_win.c', 'src/binary_reader.c', 'src/stb_ds.c', 'src/log.c')]
    prefix = ['taskset', '-c', os.environ.get('FROGGY_AUDIO_CPU', '3'), 'nice', '-n', '15',
              'prlimit', '--as=805306368', '--']
    start = time.monotonic()
    commands = []
    with (out / 'build.log').open('w') as log:
        def run(command):
            commands.append(list(map(str, command)))
            log.write(shlex.join(commands[-1]) + '\n')
            log.flush()
            try:
                subprocess.run(prefix + commands[-1], stdout=log, stderr=subprocess.STDOUT, check=True)
            except subprocess.CalledProcessError:
                print((out / 'build.log').read_text()[-10000:], flush=True)
                raise
        objects = []
        for source in sources:
            # GCC 12's ranger/VRP is pathological on this miniaudio unit.
            # Retain the other O2 passes; this is not a device-side build.
            local_flags=flags+(['-fno-tree-vrp'] if source.name=='ma_audio_system.c' else [])
            obj = out / (source.name + '.o')
            identity = hashlib.sha256(source.read_bytes() + (HERE / 'sdl_pull.inc').read_bytes() +
                                      (staged / 'ma_audio_system.h').read_bytes() +
                                      json.dumps(local_flags).encode()).hexdigest()
            key = obj.with_suffix('.key')
            if not obj.exists() or not key.exists() or key.read_text() != identity:
                print('AUDIO_COMPILE', args.target, source.name, flush=True)
                run([cc, *local_flags, '-c', source, '-o', obj])
                key.write_text(identity)
            objects.append(obj)
        binary = out / 'audio-probe'
        run([cc, '-Wl,--gc-sections', *objects, *libs, '-o', binary])
    report = dict(target=args.target, cpu=os.environ.get('FROGGY_AUDIO_CPU', '3'),
                  elapsed_seconds=time.monotonic() - start, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  binary_bytes=binary.stat().st_size, commands=commands, no_private_inputs=True)
    (out / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    if args.target == 'mips':
        with (out / 'elf.txt').open('w') as output:
            subprocess.run([str(sdk / 'host/bin/mipsel-buildroot-linux-musl-readelf'),
                            '-h', '-A', '-l', '-d', str(binary)], stdout=output, check=True)
    print(binary, flush=True)


if __name__ == '__main__':
    main()
