#!/usr/bin/env python3
"""Collect focused regressions separately from gameplay timing/recording."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    commands = [
        ('config', [sys.executable, 'tools/hwtest/emulation_config_smoke.py']),
        ('video', ['sh', 'tools/hwtest/video_smoke.sh']),
        ('stretch', ['sh', 'tools/hwtest/stretch_video_smoke.sh']),
        ('production-patches', ['sh', 'tools/hwtest/video_patch_smoke.sh']),
        ('core-api', ['sh', 'tools/hwtest/core_api_smoke.sh']),
        ('remote-ui-input-usb-network', ['bash', 'tools/hwtest/run_remote_validation.sh']),
        ('python-syntax', [sys.executable, '-m', 'py_compile',
             'tools/hwtest/emulation_benchmark.py', 'tools/hwtest/emulation_ui_smoke.py',
             'tools/hwtest/qmp_inspect.py', 'tools/hwtest/run_performance_suite.py',
             'tools/hwtest/export_emulation_evidence.py',
             'tools/hwtest/check_emulation_ui_result.py', __file__]),
        ('shellcheck', ['shellcheck', '-S', 'warning',
             'package/lilsmoke-scripts/src/lilsmoke-retroarch',
             'tools/hwtest/build_baseline_sdl.sh', 'tools/hwtest/stretch_video_smoke.sh']),
    ]
    results = []
    for name, command in commands:
        with (args.out/(name+'.log')).open('wb') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        results.append(dict(name=name, command=command, exit_code=result.returncode))
        print(f'{name}: exit={result.returncode}', flush=True)
    (args.out/'results.json').write_text(json.dumps(results, indent=2))
    return int(any(row['exit_code'] for row in results))

if __name__ == '__main__':
    raise SystemExit(main())
