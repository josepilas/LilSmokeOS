#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Serial, CPU-pinned, bounded compiler tests of the actual public renderer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import statistics
import subprocess

from make_span_div_patch import transform
from make_occlusion_patch import transform as occlusion_transform

HERE = Path(__file__).resolve().parent
SDK = Path('/root/lilsmoke-os/build/buildroot/output')


def budget():
    os.nice(10)
    os.sched_setaffinity(0, {3})
    resource.setrlimit(resource.RLIMIT_AS, (768 * 1024 * 1024,) * 2)


def run(command, **kwargs):
    return subprocess.check_output(list(map(str, command)), preexec_fn=budget,
        stderr=subprocess.STDOUT, text=True, timeout=600, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--optimization', choices=('division', 'occlusion'), default='occlusion')
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(exist_ok=False, parents=True)
    source = args.source.resolve()
    original = (source / 'src/pc/gfx/gfx_soft.c').read_text()
    after = args.out / 'gfx_soft-after.c'
    after.write_text((transform if args.optimization == 'division' else occlusion_transform)(original))
    # Relative renderer includes resolve against the existing source, never a copy.
    includes = ['-I' + str(source / p) for p in
        ('src/pc/gfx', 'include', 'src', '.', 'build/original-mips/include')]
    defines = ['-D_LANGUAGE_C', '-DVERSION_US', '-DNON_MATCHING', '-DAVOID_UB',
        '-DTARGET_LINUX', '-DENABLE_SOFTRAST', '-DF3DEX_GBI_2E', '-DNO_SEGMENTED_MEMORY']
    results = {}
    for target in ('host', 'mips'):
        for bits in (16, 32):
            outputs = {}
            binaries = {}
            for version, renderer in (('before', source / 'src/pc/gfx/gfx_soft.c'), ('after', after)):
                binary = args.out / f'{target}-{bits}-{version}'
                cc = 'cc' if target == 'host' else SDK / 'host/bin/mipsel-buildroot-linux-musl-gcc'
                command = [cc, '-std=gnu11', '-O3', '-fno-fast-math', '-fno-strict-aliasing',
                    '-fwrapv', *includes, *defines, '-DGFX_REFERENCE="' + str(renderer) + '"',
                    HERE / 'span_div_benchmark.c', source / 'src/pc/gfx/gfx_cc.c', '-lm', '-o', binary]
                if bits == 16:
                    command += ['-DCONVERT']
                if target == 'mips':
                    command += ['-mips32r2', '-mtune=74kc', '-mabi=32', '-mfp32',
                        '-mhard-float', '-mdspr2', '-mno-shared', '-mplt', '-static']
                (args.out / f'{target}-{bits}-{version}-compile.log').write_text(run(command))
                binaries[version] = [binary, '3000'] if target == 'host' else ['qemu-mipsel', '-cpu', '74Kf', '-R', '134217728', binary, '1000']
                outputs[version] = []
            # Interleave samples to avoid grouping all warm/system load effects.
            for sample in range(5):
                for version in (('before', 'after') if sample % 2 == 0 else ('after', 'before')):
                    outputs[version].append(run(binaries[version]))
            for version, samples in outputs.items():
                (args.out / f'{target}-{bits}-{version}.log').write_text(''.join(samples))
            eq = {re.search(r'RASTER_EQ.*', text)[0] for samples in outputs.values() for text in samples}
            assert len(eq) == 1, eq
            fuzz = {re.search(r'FUZZ_EQ.*', text)[0] for samples in outputs.values() for text in samples}
            assert len(fuzz) == 1, fuzz
            times = {v: {f'{kind}-{k}': statistics.median(float(t) for s in samples for h, t in
                re.findall(kind + r'_TIME hidden=(\d+) seconds=([\d.]+)', s) if int(h) == k)
                for kind in ('RASTER', 'SHORT') for k in range(3)} for v, samples in outputs.items()}
            results[f'{target}-{bits}'] = dict(equivalence=eq.pop(), fuzz_equivalence=fuzz.pop(), samples=5, times=times,
                after_over_before={k: times['after'][k] / times['before'][k] for k in times['before']})
            print(target, bits, results[f'{target}-{bits}'], flush=True)
    report = dict(passed=True, optimization=args.optimization, timing_clock='CLOCK_PROCESS_CPUTIME_ID', source_sha256=hashlib.sha256(original.encode()).hexdigest(),
        after_sha256=hashlib.sha256(after.read_bytes()).hexdigest(), cpu=3, nice=10,
        compiler_as_mib=768, physical_fps_measured=False, results=results)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
