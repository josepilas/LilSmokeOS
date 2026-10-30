#!/usr/bin/env python3
"""Run the exact stock restart loop with shell-only app/stock-display fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

P = Path(__file__).resolve().parents[2]
SOURCE = P / 'board/segam-m8-v6-stock/m8-launch.sh'
PRELUDE = '''#!/bin/sh
set -u
ROOT=$1
BASE=$ROOT
LOCK=$ROOT/lock
loading_dir=$ROOT/loading
loading_disabled=yes
logo_minimum=0
mkdir -p "$ROOT/run" "$LOCK" "$loading_dir"
export LILSMOKE_UI_WARM_RETURN=1 LILSMOKE_UI_SUPERVISOR_TOKEN=999:1
fallback() { echo UNEXPECTED_FALLBACK; exit 1; }
sleep() {
    count=$(cat "$ROOT/count")
    [ "$count" -lt 9 ] || exit 0
}
chroot() {
    count=$(cat "$ROOT/count" 2>/dev/null || echo 0)
    count=$((count + 1))
    echo "$count" > "$ROOT/count"
    warm=missing
    token=missing
    for argument in "$@"; do
        case "$argument" in
            LILSMOKE_UI_WARM_RETURN=*) warm=${argument#*=} ;;
            LILSMOKE_UI_SUPERVISOR_TOKEN=*) token=${argument#*=} ;;
        esac
    done
    echo "$count $warm $token" >> "$ROOT/rows"
    case "$count" in
        2|5|6) echo "$token" > "$ROOT/run/lilsmoke-ui-app-handoff" ;;
        4) echo stale:receipt > "$ROOT/run/lilsmoke-ui-app-handoff" ;;
    esac
    case "$count" in 5) return 9 ;; 6) return 75 ;; 7) return 70 ;; 8) return 1 ;; esac
    return 0
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    source = SOURCE.read_text()
    loop = 'runtime_failures=0\n# No inherited' + source.split('runtime_failures=0\n# No inherited', 1)[1]
    report = dict(passed=False, supervisor_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  exact_production_loop=True, actual_apps=False, sdk_writes=False, sd_access=False)
    try:
        subprocess.run(['sh', '-n', str(SOURCE)], check=True)
        with tempfile.TemporaryDirectory(prefix='lilsmoke-warm-supervisor-') as temporary:
            work = Path(temporary)
            script = work / 'test.sh'
            script.write_text(PRELUDE + loop)
            result = subprocess.run(['sh', str(script), str(work)], capture_output=True, text=True, timeout=20)
            (args.out / 'supervisor.log').write_text(result.stdout + result.stderr)
            result.check_returncode()
            rows = (work / 'rows').read_text()
            (args.out / 'invocations.txt').write_text(rows)
            hints = [int(line.split()[1]) for line in rows.splitlines()]
            assert hints == [0, 0, 1, 0, 0, 1, 1, 0, 0], hints
            assert len({line.split()[2] for line in rows.splitlines()}) == 9
            assert not (work / 'run/lilsmoke-ui-app-handoff').exists()
            report.update(passed=True, hints=hints, stale_inheritance_rejected=True,
                          ui_only_exit_cold=True, stale_receipt_rejected=True,
                          success_and_failure_app_returns_warm=True, failed_start_and_exec_cold=True,
                          receipts_consumed=True, per_invocation_generation=True)
    finally:
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
