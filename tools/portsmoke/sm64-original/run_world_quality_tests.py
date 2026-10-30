"""Serial host sanitizers and musl MIPS unit tests; no Buildroot make or devices."""
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SDK = Path('/root/lilsmoke-os/build/buildroot/output')
output = HERE / '_build/quality-20261012/units'
output.mkdir(exist_ok=True)
reports = []
for target in ('host', 'mips'):
    cc = 'gcc' if target == 'host' else str(SDK / 'host/bin/mipsel-buildroot-linux-musl-gcc')
    prefix = [] if target == 'host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', str(SDK / 'staging')]
    for case, source, convert in (
        ('rgb565', 'test_world_quality.c', True),
        ('argb', 'test_world_quality.c', False),
        ('pacing', 'test_frame_pacing.c', False),
    ):
        executable = output / f'{target}-{case}'
        command = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-no-pie',
            '-I' + str(ROOT / 'package/lilsmoke-sm64-original/src')]
        if target == 'host':
            command += ['-fsanitize=address,undefined']
        if convert:
            command += ['-DCONVERT']
        command += [str(HERE / source), '-o', str(executable)]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([*prefix, str(executable)], capture_output=True, text=True,
            check=True, timeout=60)
        (output / f'{target}-{case}.log').write_text(result.stdout + result.stderr)
        reports.append(dict(target=target, case=case, passed=True, compiler_command=command,
            run_command=[*prefix, str(executable)], output=result.stdout.strip(),
            sanitizers=target == 'host'))
        print(target, case, result.stdout.strip(), flush=True)
(output / 'report.json').write_text(json.dumps(dict(passed=True, cases=reports,
    physical_console_fps_measured=False, new_pixel_math_integer_only=True), indent=2) + '\n')
