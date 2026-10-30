#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Retain bounded fresh-VM evidence before separate native-build/resume QA.

This is not full compilation acceptance. It stops only the private QA VM,
whose disposable FAT image is not subsequently treated as clean storage.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not str(out).startswith('/root/sm64-prep3-clock-vm-'):
        raise ValueError('Not a private clock QA directory')
    serial = (out / 'serial.log').read_text(errors='replace')
    first = (out / 'preparation-1.log').read_text(errors='replace')
    second = (out / 'preparation-2.log').read_text(errors='replace')
    epoch = int(re.search(r'QA_GUEST_EPOCH=(\d+)', serial).group(1))
    completed = max(map(int, re.findall(r'build_progress completed=(\d+)', second)))
    assert epoch < 315532800 and completed >= 8
    assert all(token in serial for token in ('QA_PREPARATION_EXIT_1=130', 'QA_CANCEL_ORIGINAL_PRESERVED=1'))
    assert 'SM64_STAGE assets_done' in first and 'SM64_STAGE checkpoint_ready' in first
    assert 'SM64_STAGE resume_assets' in second and 'SM64_STAGE unpack_begin' not in second
    assert 'SM64_STAGE plan_done' in second and 'SM64_STAGE compile_begin' in second
    assert 'SM64_PREPARATION_ERROR' not in second
    matches = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal(): continue
        try:
            command = (proc / 'cmdline').read_bytes().split(b'\0')
            if (Path(os.fsdecode(command[0])).name == 'qemu-system-mipsel' and
                    os.fsencode('unix:' + str(out) + '/qmp.sock,server=on,wait=off') in command and
                    os.fsencode('file=' + str(out / 'PRIVATE-application.fat') + ',if=none,id=appdisk,format=raw') in command):
                matches.append(int(proc.name))
        except (OSError, IndexError):
            continue
    if len(matches) != 1:
        raise RuntimeError('Expected exactly this isolated private QA VM')
    result = dict(passed=True, fresh_rom_extraction=True, guest_clock_epoch=epoch,
        planning_completed=True, native_compilation_started=True,
        completed_tasks_at_stop=completed, cancel_preserves_original=True,
        resumes_without_reextracting=True, full_game_compiled=False,
        requires_separate_full_build_and_resume_acceptance=True,
        cpus=1, ram_mib=120, process_limit_kib=92160,
        physical_card_modified=False, storage_cleanly_unmounted=False,
        stopped_by_test_supervisor=True,
        runtime_sources={name: hashlib.sha256((out / 'Test App/runtime' / name).read_bytes()).hexdigest()
            for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py')})
    (out / 'checkpoint-report.json').write_text(json.dumps(result, indent=2) + '\n')
    os.kill(matches[0], signal.SIGTERM)
    print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
