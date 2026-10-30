#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Private QA only: retain byte-verified C jobs across the narrow ASM fix."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from build_progress import classify_command

C_TARGETS = (
    'build/original-mips/src/game/behavior_actions.o',
    'build/original-mips/src/game/camera.o',
    'build/original-mips/actors/group12.o',
    'build/original-mips/src/pc/gfx/gfx_soft.o',
    'build/original-mips/levels/castle_inside/leveldata.o',
    'build/original-mips/assets/mario_anim_data.o',
)
ASM = b'$(CC) -mips32r2 -mabi=32 -mfp32 -mhard-float -mdspr2 -c -x assembler-with-cpp'
FIXED_ASM = ASM.replace(b'$(CC) ', b'$(CC) -nostdinc ', 1)
BUILDER_CALL = b'                game, extracted = builder(stage, progress, cancel)\n'
RECOVERY = b'''                    # An interrupted final copy is not a committed cache. Keep
                    # verified source work, but regenerate this private output.
                    pending_game = stage / 'game'
                    if pending_game.exists() or pending_game.is_symlink():
                        regular(pending_game, 32 * 1024 * 1024)
                        pending_game.unlink()
'''


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def validate_delta(old, new):
    if (old.count(ASM) != 1 or new.count(FIXED_ASM) != 1 or
            new.replace(FIXED_ASM, ASM, 1) != old):
        raise ValueError('Only the ASM no-system-header fix may reuse these C jobs')


def validate_recovery_delta(old, new):
    if old.count(BUILDER_CALL) != 1 or new != old.replace(BUILDER_CALL, RECOVERY + BUILDER_CALL, 1):
        raise ValueError('Only uncommitted-game cleanup may change cache recovery')


def reuse(previous, stage, reference, runtime, out):
    previous = previous.resolve()
    if not str(previous).startswith('/root/sm64-prep5-resume-vm-'):
        raise ValueError('Not the isolated predecessor QA image')
    old_app = previous / 'Test App'
    old_runtime = old_app / 'runtime'
    old_module, new_module = old_runtime / 'native_build.py', runtime / 'native_build.py'
    validate_delta(old_module.read_bytes(), new_module.read_bytes())
    validate_recovery_delta((old_runtime / 'cook.py').read_bytes(), (runtime / 'cook.py').read_bytes())
    for module in ('resume.py', 'build_progress.py'):
        if digest(old_runtime / module) != digest(runtime / module):
            raise ValueError('Other production code changed')
    manifest = json.loads((runtime / 'distribution-manifest.json').read_text())
    for name, row in manifest['files'].items():
        if name in ('meta.json', 'runtime/native_build.py', 'runtime/cook.py'):
            continue
        if digest(old_app / name) != row['sha256']:
            raise ValueError('Predecessor immutable runtime differs: ' + name)
    serial = (previous / 'serial.log').read_text()
    log_path = previous / 'failure-build.log'
    build_log = log_path.read_text()
    if (not all(token in serial for token in ('QA_PREPARATION_EXIT_1=130',
            'QA_PREPARATION_EXIT_2=1', 'QA_STORAGE_UMOUNT=0')) or
            'cc1: error: no include path in which to search for stdc-predef.h' not in build_log or
            re.search(r'Out of memory|oom-kill|Killed process|virtual memory exhausted', serial, re.I)):
        raise ValueError('Predecessor did not close cleanly at the known header failure')
    if (int(re.search(r'MemTotal:\s+(\d+)', serial).group(1)) > 112640 or
            int(re.search(r'SwapTotal:\s+(\d+)', serial).group(1)) != 0 or
            int(re.search(r'QA_GUEST_EPOCH=(\d+)', serial).group(1)) >= 315532800):
        raise ValueError('Predecessor memory/swap/clock differs')
    tasks = [task for line in build_log.splitlines() if (task := classify_command(line))]
    compiled = [task.target for task in tasks if task.kind == 'compile']
    if (compiled != list(C_TARGETS) or tasks[-1].kind != 'assembly' or
            tasks[-1].target != 'build/original-mips/sound/sound_data.o'):
        raise ValueError('Predecessor did not execute precisely the six completed C jobs')
    disk = previous / 'PRIVATE-application.fat'
    fsck = subprocess.run(['/usr/sbin/fsck.vfat', '-n', str(disk)], capture_output=True, text=True)
    if fsck.returncode:
        raise ValueError('Predecessor FAT is not clean')
    out.mkdir(exist_ok=False)
    (out / 'filesystem.log').write_text(fsck.stdout + fsck.stderr)
    source_hashes = {}
    for target in C_TARGETS:
        private = out / target
        private.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['mcopy', '-i', str(disk),
            '::/Test App/.sm64-cache/working-imported/source/' + target, str(private)], check=True)
        expected = digest(reference / target)
        if digest(private) != expected:
            raise ValueError('VM object differs from independently completed native build: ' + target)
        shutil.copyfile(private, stage / 'source' / target)
        shutil.copystat(reference / target, stage / 'source' / target)
        source_hashes[target] = expected
    proof = dict(passed=True, filesystem_clean=True, no_oom=True,
        only_asm_header_fix=True, all_immutable_runtime_equal_except_bounded_fix=True,
        uncommitted_game_recovery_fix=True,
        new_cook_source_sha256=digest(runtime / 'cook.py'),
        cpus=1, ram_mib=120, swap_kib=0,
        completed_compiler_targets=list(C_TARGETS), object_sha256=source_hashes,
        old_native_source_sha256=digest(old_module), new_native_source_sha256=digest(new_module),
        reference_complete_game_sha256=digest(reference / 'build/original-mips/sm64.us.f3dex2e'),
        predecessor_serial_sha256=digest(previous / 'serial.log'),
        predecessor_build_log_sha256=digest(log_path),
        complete_predecessor_preparation=False, private_objects_not_distributable=True)
    (out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('previous', 'snapshot', 'runtime', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    stage = args.out / 'PRIVATE-stage'
    shutil.copytree(args.snapshot / 'PRIVATE-stage', stage)
    print(json.dumps(reuse(args.previous, stage, args.snapshot / 'PRIVATE-stage/source',
        args.runtime, args.out / 'PRIVATE-objects'), indent=2))
