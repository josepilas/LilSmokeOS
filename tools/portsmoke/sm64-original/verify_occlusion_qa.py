#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Extra fuzz, ASan/UBSan and patch-chain checks without copying a source tree."""
import argparse
import difflib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile

from make_occlusion_patch import transform
from test_span_div import HERE, budget


def runtime_budget():
    # ASan's shadow reservation is virtual, not compiler memory. Compilation
    # still uses the same 768 MiB address-space bound as the MIPS compiler.
    os.nice(10)
    os.sched_setaffinity(0, {3})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    source = args.source.resolve()
    before = (source / 'src/pc/gfx/gfx_soft.c').read_text()
    with tarfile.open(args.runtime / 'source.tar.gz') as archive:
        assert archive.extractfile('src/pc/gfx/gfx_soft.c').read() == before.encode()
    after = transform(before)
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        'a/src/pc/gfx/gfx_soft.c', 'b/src/pc/gfx/gfx_soft.c'))
    assert patch.encode() == (HERE / '0009-block-occlusion.patch').read_bytes()
    scratch = out / 'patch-check/src/pc/gfx'
    scratch.mkdir(parents=True)
    (scratch / 'gfx_soft.c').write_text(before)
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(HERE / '0009-block-occlusion.patch')],
        cwd=out / 'patch-check', check=True, capture_output=True)
    assert (scratch / 'gfx_soft.c').read_text() == after
    results = {}
    includes = ['-I' + str(source / p) for p in ('src/pc/gfx', 'include', 'src', '.', 'build/original-mips/include')]
    defines = ['-D_LANGUAGE_C', '-DVERSION_US', '-DNON_MATCHING', '-DAVOID_UB',
        '-DTARGET_LINUX', '-DENABLE_SOFTRAST', '-DF3DEX_GBI_2E', '-DNO_SEGMENTED_MEMORY']
    for bits in (16, 32):
        eq = set()
        for version, text in (('before', before), ('after', after)):
            renderer = out / ('gfx-' + version + '.c')
            if not renderer.exists():
                renderer.write_text(text)
            binary = out / f'asan-{bits}-{version}'
            command = ['cc', '-O1', '-g', '-std=gnu11', '-fno-fast-math', '-fno-strict-aliasing',
                '-fwrapv', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                *includes, *defines, '-DGFX_REFERENCE="' + str(renderer) + '"', HERE / 'span_div_benchmark.c',
                source / 'src/pc/gfx/gfx_cc.c', '-lm', '-o', binary]
            if bits == 16:
                command.append('-DCONVERT')
            with (out / f'asan-{bits}-{version}-compile.log').open('w') as log:
                subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT,
                    preexec_fn=budget, check=True, timeout=600)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
            completed = subprocess.run([str(binary), '1'], env=env, stderr=subprocess.STDOUT,
                stdout=subprocess.PIPE, preexec_fn=runtime_budget, text=True, timeout=300)
            log = completed.stdout
            (out / f'asan-{bits}-{version}.log').write_text(log)
            completed.check_returncode()
            assert 'runtime error:' not in log and 'ERROR: AddressSanitizer' not in log
            eq.add(tuple(re.findall(r'^(?:RASTER_EQ|FUZZ_EQ).*$', log, re.M)))
        assert len(eq) == 1
        results[str(bits)] = dict(sanitizer_faults=0, equal=eq.pop())
    report = dict(passed=True, public_source_matches_snapshot=True, patch_fuzz_zero=True,
        source_before_sha256=hashlib.sha256(before.encode()).hexdigest(),
        source_after_sha256=hashlib.sha256(after.encode()).hexdigest(),
        formats=results, compiler_as_mib=768, runtime_asan_virtual_shadow_unlimited=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
