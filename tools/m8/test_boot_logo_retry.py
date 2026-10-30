#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise the actual supervisor's optional-logo retry block, without devices."""
import os
from pathlib import Path
import subprocess
import tempfile

P = Path(__file__).resolve().parents[2]
source = (P / 'board/segam-m8-v6-stock/m8-launch.sh').read_text()
start = source.index('runtime_failures=0\n# No inherited')
end = source.index('    if [ -f "$ROOT/run/lilsmoke-poweroff-request" ] &&', start)
loop = source[start:end]
for shell in (['sh'], ['busybox', 'sh']):
    for status in (0, 70, 139):
        with tempfile.TemporaryDirectory(prefix='m8-logo-retry-') as temporary:
            work = Path(temporary)
            for directory in ('root/run', 'base', 'lock'):
                (work / directory).mkdir(parents=True)
            for name in ('root/run/m8-boot-logo.state', 'lock/boot-logo.state', 'base/libm8-boot-logo.so'):
                (work / name).write_text('fixture\n')
            (work / 'count').write_text('0\n')
            script = '''set -u
ROOT="$QA_ROOT/root"; BASE="$QA_ROOT/base"; LOCK="$QA_ROOT/lock"
loading_dir="$LOCK/loading"; logo_minimum=1; loading_disabled=no
start_loading() { :; }
chroot() {
    count=$(cat "$QA_ROOT/count")
    count=$((count+1)); printf '%s\n' "$count" >"$QA_ROOT/count"
    for argument in "$@"; do
        case "$argument" in LD_PRELOAD=*) printf '%s\n' "$argument" >>"$QA_ROOT/preloads" ;; esac
    done
    [ "$count" != 1 ] || return "$QA_STATUS"
    return 0
}
'''+loop+'''
    [ "$(cat "$QA_ROOT/count")" -lt 2 ] || break
done
'''
            env = dict(os.environ, QA_ROOT=str(work), QA_STATUS=str(status))
            subprocess.run(shell, input=script, text=True, env=env, check=True, timeout=10)
            assert (work / 'preloads').read_text().splitlines() == [
                'LD_PRELOAD=/os/libm8-boot-logo.so:/os/libm8-display.so',
                'LD_PRELOAD=/os/libm8-display.so']
            assert not (work / 'root/run/m8-boot-logo.state').exists()
            assert not (work / 'lock/boot-logo.state').exists()
print('PASS actual supervisor retry block: success, exit70 and crash139 never reapply the logo gate; POSIX sh and BusyBox ash.')
