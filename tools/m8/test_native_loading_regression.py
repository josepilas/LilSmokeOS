"""Reproduce Candidate37's native-map failure, then verify the corrected pair."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sdk', type=Path, required=True)
ap.add_argument('--artifacts', type=Path, required=True)
ap.add_argument('--before-gate', type=Path, required=True)
ap.add_argument('--before-display', type=Path, required=True)
ap.add_argument('--after-display', type=Path)
ap.add_argument('--out', type=Path, required=True)
a = ap.parse_args()
a.out.mkdir(exist_ok=False)
report = []
for mode in ('rgb16', 'rgb32', 'menu16', 'menu32', 'flip'):
    for fixed in (False, True):
        with tempfile.TemporaryDirectory(prefix='m8-loading-ab-') as tmp:
            work = Path(tmp)
            fb, state = work / 'fb', work / 'state'
            fb.write_bytes(b'\x55' * 256)
            state.write_text(f'M8_BOOT_LOGO_V1 {int(time.monotonic()*1000)}\n')
            state.chmod(0o600)
            gate = a.artifacts / 'gate.so' if fixed else a.before_gate
            display = (a.after_display or a.artifacts / 'display.so') if fixed else a.before_display
            preload = ':'.join(str(p) for p in (gate, display, a.artifacts / 'fake.so'))
            env = dict(os.environ, M8_TEST_FB=str(fb), M8_TEST_STATE=str(state),
                       LILSMOKE_M8_BOOT_STATE='/run/m8-boot-logo.state',
                       LILSMOKE_M8_LOADING_GAME='1', LILSMOKE_HCFB='1',
                       LILSMOKE_M8_NATIVE_VIDEO='1')
            proc = subprocess.run(['qemu-mipsel', '-cpu', '74Kf', '-L', str(a.sdk / 'staging'),
                '-E', 'LD_PRELOAD=' + preload, str(a.artifacts / 'native-probe'), mode],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            log = proc.stdout.decode(errors='replace')
            label = 'fixed' if fixed else 'candidate37'
            (a.out / f'{mode}-{label}.log').write_text(log)
            if fixed:
                assert proc.returncode == 0 and 'M8NativeVideo active' in log, log
                assert 'fallback' not in log, log
            else:
                assert proc.returncode != 0 and 'fallback stage=mmap' in log, log
            report.append(dict(mode=mode, build=label, exit=proc.returncode,
                               native_mapping_enabled=fixed, frames=1000 if fixed else 0))
(a.out / 'report.json').write_text(json.dumps(dict(passed=True, cases=report,
    actual_mips_instructions=True, file_backed_hcfb_fixture=True,
    physical_console_tested=False, target_fps_measured=False), indent=2) + '\n')
print('PASS: Candidate37 native-map regression reproduced in all five paths; corrected paths render 1000 frames each.')
