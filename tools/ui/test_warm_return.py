#!/usr/bin/env python3
"""Small host ASan/UBSan and MIPS cache/hint regression, no SDK writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

P = Path(__file__).resolve().parents[2]
S = P / 'package/lilsmoke-ui/src/src'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(passed=False, sdk_writes=False, sd_access=False, cpus=1, commands=[])
    sources = [P / 'tools/ui/warm-return-test.cpp', S / 'ROMScanner.cpp', S / 'Log.cpp']
    inputs = sources + [S / 'UiWarmReturn.h', S / 'main.cpp', P / 'board/segam-m8-v6-stock/m8-launch.sh']
    report['source_sha256'] = {str(p.relative_to(P)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    try:
        for arch in ('host', 'mips'):
            cxx = 'g++' if arch == 'host' else str(args.sdk / 'host/bin/mipsel-buildroot-linux-musl-g++')
            flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-I' + str(S),
                     '-I' + str(P / 'package/lilsmoke-scripts/src')]
            prefix = []
            if arch == 'host':
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
            else:
                prefix = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(args.sdk / 'target')]
            for name in ('warm-return-test', 'romscanner_metadata_smoke'):
                test = P / ('tools/ui/warm-return-test.cpp' if name == 'warm-return-test' else 'tools/hwtest/' + name + '.cpp')
                binary = args.out / (arch + '-' + name)
                compile_command = [cxx, *flags, str(test), *map(str, sources[1:]), '-o', str(binary)]
                for phase, command in [('compile', compile_command), ('run', [*prefix, str(binary)])]:
                    report['commands'].append(command)
                    with (args.out / (arch + '-' + name + '-' + phase + '.log')).open('w') as log:
                        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180,
                                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
                print(arch, name, 'PASS', flush=True)
        report['passed'] = True
    finally:
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
