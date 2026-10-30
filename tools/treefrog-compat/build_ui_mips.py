#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Isolated MIPS UI payload build using the existing SDK without installing it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

P = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = [P / 'package/lilsmoke-ui/src/Makefile', P / 'tools/ui/build_isolated.sh']
    for folder in ('lilsmoke-ui/src', 'lilsmoke-scripts/src', 'lilsmoke-media/src',
                   'lilsmoke-usb-gamepads/src', 'lilsmoke-od-sdl/src'):
        extensions = ('.h', '.hpp', '.cpp') if folder == 'lilsmoke-ui/src' else ('.h', '.hpp')
        paths.extend(p for p in (P / 'package' / folder).rglob('*')
                     if p.is_file() and p.suffix in extensions)
    return {str(path.relative_to(P)): sha(path) for path in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, help='Compare payload and existing VM evidence by byte identity')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(passed=False, sdk=str(args.sdk), sdk_modified=False, sdk_global_rebuild=False,
                  sdk_copied=False, sd_access=False, compiler_flags='-std=c++17 -O3 -flto')
    try:
        before = inputs()
        with tempfile.TemporaryDirectory(prefix='lilsmoke-treefrog-ui-mips-') as temporary:
            work = Path(temporary)
            environment = dict(os.environ, SDK_OUTPUT=str(args.sdk), UI_BUILD_OUTPUT=str(work),
                               UI_OPT_FLAGS='-O3 -flto')
            with (args.out / 'build.log').open('w') as log:
                subprocess.run(['sh', str(P / 'tools/ui/build_isolated.sh')], cwd=P, env=environment,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
            if inputs() != before:
                raise RuntimeError('Build inputs changed during compilation')
            payload = args.out / 'payload'
            payload.mkdir()
            for name in ('lilsmoke-ui', 'lilsmoke-session'):
                shutil.copyfile(work / (name + '.stripped'), payload / name)
                (payload / name).chmod(0o755)
            readelf = args.sdk / 'host/bin/mipsel-buildroot-linux-musl-readelf'
            elf = subprocess.check_output([str(readelf), '-hAWd', str(payload / 'lilsmoke-ui')], text=True)
            (args.out / 'ui-elf.txt').write_text(elf)
            if 'Machine:                           MIPS' not in elf or 'little endian' not in elf:
                raise RuntimeError('Unexpected UI target architecture')
            report.update(passed=True, source_inputs=before,
                          compiler=subprocess.check_output([str(args.sdk / 'host/bin/mipsel-buildroot-linux-musl-g++'),
                                                            '--version'], text=True).splitlines()[0],
                          artifacts={name: dict(path=str(payload / name), sha256=sha(payload / name),
                                                bytes=(payload / name).stat().st_size)
                                     for name in ('lilsmoke-ui', 'lilsmoke-session')},
                          unstripped_ui_sha256=sha(work / 'lilsmoke-ui'),
                          temporary_build_bytes=sum(p.stat().st_size for p in work.rglob('*') if p.is_file()),
                          temporary_build_removed=True)
            if args.baseline:
                baseline = json.loads((args.baseline / 'build-report.json').read_text())
                baseline_ui = args.baseline / 'payload/lilsmoke-ui'
                baseline_sha = sha(baseline_ui)
                if not baseline['passed'] or baseline['artifacts']['lilsmoke-ui']['sha256'] != baseline_sha:
                    raise RuntimeError('Baseline payload is not consistent with its build report')
                identical = baseline_sha == sha(payload / 'lilsmoke-ui')
                report['baseline_comparison'] = dict(
                    build_report=str(args.baseline / 'build-report.json'),
                    baseline_ui_sha256=baseline_sha, byte_identical=identical,
                    changed_source_inputs=[name for name, digest in before.items()
                                           if baseline['source_inputs'].get(name) != digest],
                    excluded_uncompiled_baseline_inputs=sorted(set(baseline['source_inputs']) - set(before)))
                evidence = args.baseline / 'actual-ui-vm/report.json'
                if evidence.is_file():
                    qualification = json.loads(evidence.read_text())
                    if qualification['passed'] and qualification['actual_ui_sha256'] == baseline_sha and identical:
                        report['existing_actual_ui_vm_evidence'] = dict(
                            report=str(evidence), report_sha256=sha(evidence),
                            tested_ui_sha256=baseline_sha, reusable_by_byte_identity=True,
                            rerun=False, scope=qualification['qualification'],
                            original_engine_executed=qualification['original_engine_executed'])
    except Exception as error:
        report.update(passed=False, error=str(error))
        raise
    finally:
        (args.out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'source_inputs'}, indent=2))


if __name__ == '__main__':
    main()
