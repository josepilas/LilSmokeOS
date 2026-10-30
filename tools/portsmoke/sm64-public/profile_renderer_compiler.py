#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Profile the exact private native-MIPS renderer command; never SD/release data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time

from build_progress import classify_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--no-gcc-reports', action='store_true')
    parser.add_argument('--target', default='build/original-mips/src/pc/gfx/gfx_soft.o',
        choices=('build/original-mips/src/pc/gfx/gfx_soft.o',
                 'build/original-mips/levels/castle_inside/leveldata.o',
                 'build/original-mips/assets/mario_anim_data.o'))
    parser.add_argument('--data-no-optimization', action='store_true')
    parser.add_argument('--no-macro-trace', action='store_true')
    args = parser.parse_args()
    if args.data_no_optimization and '/gfx/' in args.target:
        parser.error('Do not reduce executable renderer optimization')
    args.out.mkdir(exist_ok=False)
    work = args.out / 'source'
    shutil.copytree(args.snapshot / 'PRIVATE-stage/source', work)
    command = None
    for line in (args.snapshot / 'PRIVATE-stage/build.log').read_text().splitlines():
        task = classify_command(line)
        if task and task.target == args.target and task.kind == 'compile':
            command = shlex.split(line)
            break
    if command is None: raise RuntimeError('No exact renderer command in accepted native build')
    reference = work / args.target
    original_hash = hashlib.sha256(reference.read_bytes()).hexdigest()
    result = args.out / 'PRIVATE-profiled-renderer.o'
    command[command.index('-o') + 1] = str(result)
    if args.data_no_optimization:
        command += ['-O0', '-fno-unit-at-a-time']
    if args.no_macro_trace:
        command += ['-ftrack-macro-expansion=0']
    if not args.no_gcc_reports:
        command += ['-fmem-report', '-ftime-report']
    sdk = '/root/lilsmoke-os/build/buildroot/output'
    env = dict(os.environ, QEMU_CPU='74Kf', QEMU_LD_PREFIX=sdk + '/staging',
        QEMU_GUEST_BASE='0',
        LILSMOKE_APP_DIR=str(args.runtime.parent),
        LD_LIBRARY_PATH=str(args.runtime / 'lib') + ':' + sdk + '/staging/usr/lib:' + sdk + '/staging/lib')
    env.pop('LD_PRELOAD', None)
    started = time.monotonic()
    samples = []
    peaks = {}
    with (args.out / 'compiler.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
        while process.poll() is None:
            for proc in Path('/proc').iterdir():
                if not proc.name.isdecimal(): continue
                try:
                    if (proc / 'cwd').resolve() != work: continue
                    virtual = rss = 0
                    active = False
                    native_image = False
                    for line in (proc / 'smaps').read_text().splitlines():
                        match = re.match(r'^([a-f0-9]+)-([a-f0-9]+) ([rwxps-]+) ', line)
                        if match:
                            lo, hi = int(match[1], 16), int(match[2], 16)
                            active = hi <= 0x100000000 and match[3] != '---p' and 'tcg' not in line.lower()
                            if active:
                                virtual += (hi - lo) // 1024
                                native_image |= '/runtime/compiler/' in line
                        elif active and line.startswith('Rss:'):
                            rss += int(line.split()[1])
                    if native_image:
                        previous = peaks.get(proc.name, (0, 0))
                        if virtual > previous[0] or rss > previous[1]:
                            peaks[proc.name] = (max(virtual, previous[0]), max(rss, previous[1]))
                            samples.append(dict(pid=int(proc.name), guest_virtual_kib=virtual,
                                guest_resident_kib=rss, command=(proc / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')))
                except (OSError, ProcessLookupError):
                    continue
            time.sleep(0.025)
        status = process.wait()
    report = dict(status=status, elapsed_seconds=round(time.monotonic() - started, 2),
        extra_gcc_reports=not args.no_gcc_reports,
        object_target=args.target, data_no_optimization=args.data_no_optimization,
        macro_trace_disabled=args.no_macro_trace,
        target='native MIPS; QEMU user mode', physical_ram_limit_test=False,
        reference_object_sha256=original_hash,
        result_object_sha256=hashlib.sha256(result.read_bytes()).hexdigest() if result.exists() else None)
    report['identical_object'] = report['result_object_sha256'] == original_hash
    report['guest_low_address_sampled_virtual_peak_kib'] = max((row['guest_virtual_kib'] for row in samples), default=None)
    report['guest_low_address_sampled_resident_peak_kib'] = max((row['guest_resident_kib'] for row in samples), default=None)
    report['mapping_sample_scope'] = 'QEMU_GUEST_BASE=0; low-32-bit mappings, excluding inaccessible reservations and named TCG regions; not whole-system RAM proof'
    (args.out / 'PRIVATE-mapping-samples.json').write_text(json.dumps(samples, indent=2) + '\n')
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if status != 0: raise RuntimeError('Native compiler profiling failed')


if __name__ == '__main__': main()
