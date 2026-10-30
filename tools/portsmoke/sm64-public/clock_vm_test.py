#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Full private BYO-ROM preparation/game on FAT32 with the M8's 1970 clock.

One MIPS CPU, 120 MiB total guest RAM, no swap and a 128 MiB virtual limit.
The physical vendor SoC, PCM driver and its timing are not emulated by Malta.
All ROMs, cooked games and the FAT image are private QA data, never releases.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
P = HERE.parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--resume-snapshot', type=Path)
    parser.add_argument('--native', type=Path)
    parser.add_argument('--fresh-checkpoint', type=Path)
    parser.add_argument('--checkpoint-only', action='store_true')
    parser.add_argument('--uncompressed-root', action='store_true',
        help='Use unchanged QA root files on a read-only ext2 block image')
    parser.add_argument('--previous-vm', type=Path,
        help='Reuse strictly verified C jobs from the clean pre-header-fix VM')
    args = parser.parse_args()
    resume_mode = args.resume_snapshot is not None
    if resume_mode and args.checkpoint_only:
        parser.error('Cold extraction checkpoint cannot use a prepared snapshot')
    if args.previous_vm and not resume_mode:
        parser.error('Previous VM reuse requires the independently completed native snapshot')
    if any((args.resume_snapshot, args.native, args.fresh_checkpoint)) and not all(
            (args.resume_snapshot, args.native, args.fresh_checkpoint)):
        parser.error('Resume QA requires the snapshot, native acceptance and fresh-clock checkpoint')
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    root_image = B / 'images/rootfs.squashfs'
    root_fstype = 'squashfs'
    if args.uncompressed_root:
        tree = out / 'root-files'
        subprocess.run(['unsquashfs', '-processors', '1', '-no-progress', '-d', str(tree),
                        str(root_image)], check=True, stdout=subprocess.DEVNULL)
        root_image = out / 'root.ext2'
        with root_image.open('xb') as image:
            image.truncate(256 * 1024 * 1024)
        subprocess.run(['mkfs.ext2', '-q', '-F', '-d', str(tree), str(root_image)], check=True)
        root_fstype = 'ext2'
    app = out / 'Test App'
    shutil.copytree(args.runtime.parent, app)
    for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py'):
        if (app / 'runtime' / name).read_bytes() != (HERE / name).read_bytes():
            raise ValueError('QA must use the current frozen package: ' + name)
    (app / 'original').mkdir(exist_ok=True)
    shutil.copyfile(args.rom, app / 'original/Owner ROM.z64')
    owner_hash = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    launcher = (app / 'SM64.sh').read_text()
    if re.findall(r'^ulimit -v (\d+)$', launcher, re.M) != ['131072']:
        raise ValueError('QA must exercise the shipped compiler address-space limit')
    forced_targets = []
    prior_compilation = None
    if resume_mode:
        native = json.loads((args.native / 'report.json').read_text())
        capture = json.loads((args.resume_snapshot / 'capture.json').read_text())
        fresh = json.loads((args.fresh_checkpoint / 'checkpoint-report.json').read_text())
        if (native.get('passed') is not True or not native.get('source_dates_in_future') or
                capture.get('generated_elf_sha256') != native.get('game_sha256') or
                fresh.get('passed') is not True or fresh.get('guest_clock_epoch', 315532800) >= 315532800):
            raise ValueError('Incomplete independent build/extraction evidence')
        binary = args.resume_snapshot / 'PRIVATE-stage/source/build/original-mips/sm64.us.f3dex2e'
        if hashlib.sha256(binary.read_bytes()).hexdigest() != native['game_sha256']:
            raise ValueError('Private snapshot executable changed')
        stage = app / '.sm64-cache/working-imported'
        shutil.copytree(args.resume_snapshot / 'PRIVATE-stage', stage)
        if hashlib.sha256((stage / 'game').read_bytes()).hexdigest() != native['game_sha256']:
            raise ValueError('Snapshot must retain the interrupted, uncommitted final copy')
        marker = json.loads((stage / 'assets-ready.json').read_text())
        (stage.parent / 'active.json').write_text(json.dumps(dict(schema=1,
            recipe=marker['recipe'], rom_sha1=marker['rom_sha1'], generation=stage.name)))
        # Dependency files are private generated Make syntax. Rebase only the
        # known runtime prefix; all source/asset bytes remain unchanged.
        old_prefix = str((args.native / 'Test App/runtime').resolve()).replace(' ', '\\ ')
        new_prefix = '/portdisk/Test\\ App/runtime'
        for dependency in (stage / 'source/build').rglob('*.d'):
            value = dependency.read_text()
            dependency.write_text(value.replace(old_prefix, new_prefix))
            os.utime(dependency, (1893456004, 1893456004))
        forced_targets = ['build/original-mips/src/pc/gfx/gfx_soft.o',
                          'build/original-mips/sound/sound_data.o',
                          'build/original-mips/levels/castle_inside/leveldata.o',
                          'build/original-mips/assets/mario_anim_data.o',
                          'build/original-mips/actors/group12.o',
                          'build/original-mips/src/game/behavior_actions.o',
                          'build/original-mips/src/game/camera.o',
                          'build/original-mips/sm64.us.f3dex2e']
        if args.previous_vm:
            from reuse_vm_compiles import reuse
            prior_compilation = reuse(args.previous_vm, stage,
                args.resume_snapshot / 'PRIVATE-stage/source', args.runtime,
                out / 'PRIVATE-previous-objects')
            forced_targets = [target for target in forced_targets
                if target not in prior_compilation['completed_compiler_targets']]
        for target in forced_targets:
            (stage / 'source' / target).unlink()
    disk = out / 'PRIVATE-application.fat'
    with disk.open('xb') as image:
        image.truncate(768 * 1024 * 1024)
    subprocess.run(['mkfs.vfat', '-F', '32', str(disk)], check=True, capture_output=True)
    subprocess.run(['mcopy', '-m', '-s', '-i', str(disk), str(app), '::/'], check=True)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, runtime_libraries
    from vm_scene_smoke import Qmp
    init = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /bench /portdisk /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t vfat -o rw,umask=000 /dev/sdb /portdisk || poweroff -f
mount -t tmpfs -o size=8m tmpfs /dev/shm
date -s '1970-01-01 08:00:00'
echo QA_GUEST_EPOCH=$(date +%s)
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
export LILSMOKE_APP_DIR='/portdisk/Test App' LILSMOKE_HCFB=0 LILSMOKE_DFB_EVDEV_KEYCODES=1
export LILSMOKE_APP_ID=org.lilsmoke.sm64 LILSMOKE_PRIVATE_HARDWARE_QA=1
export LILSMOKE_SESSION_LOG=/usr/bin/qa-session-log
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LD_LIBRARY_PATH='/portdisk/Test App/runtime/lib:/usr/lib:/lib'
export SM64_ORIGINAL_TEST_FRAMES=900 SM64_ORIGINAL_RENDER_EVERY=1
export SM64_ORIGINAL_INTERACTIVE_AFTER=600 SM64_ORIGINAL_INPUT_TRACE=1
echo QA_MEMORY_BEGIN
cat /proc/meminfo
echo QA_MEMORY_END
(while :; do
    sleep 15
    for comm in /proc/[0-9]*/comm; do
        read name < "$comm" || continue
        case "$name" in python3.11|sm64-prepare|cc1|cc1plus|game|ld.so.1)
            awk '/^Name:/ {name=$2} /^VmPeak:/ {peak=$2} /^VmHWM:/ {hwm=$2}
                /^VmRSS:/ {rss=$2} END {printf "QA_PROCESS name=%s peak=%s hwm=%s rss=%s\\n", name,peak,hwm,rss}' \
                "${comm%/comm}/status" 2>/dev/null ;;
        esac
    done
done) &
for run in 1 2 3; do
    if [ "$run" = 3 ]; then export SM64_ORIGINAL_TEST_FRAMES=30; fi
    echo QA_RUN_BEGIN_$run
    /bin/sh '/portdisk/Test App/SM64.sh'
    status=$?
    echo QA_PREPARATION_EXIT_$run=$status
    if [ "$run" = 1 ] && [ -f '/portdisk/Test App/original/Owner ROM.z64' ]; then
        echo QA_CANCEL_ORIGINAL_PRESERVED=1
    fi
    if [ "$status" != 0 ] && [ "$status" != 130 ]; then break; fi
    sleep 1
done
sync
umount /portdisk
echo QA_STORAGE_UMOUNT=$?
poweroff -f
'''
    if args.checkpoint_only:
        init = init.replace('for run in 1 2 3; do', 'for run in 1 2; do')
    if resume_mode:
        init = init.replace('echo QA_CANCEL_ORIGINAL_PRESERVED=1', '''echo QA_CANCEL_ORIGINAL_PRESERVED=1
        if [ ! -e '/portdisk/Test App/.sm64-cache/working-imported/game' ] &&
           [ ! -L '/portdisk/Test App/.sm64-cache/working-imported/game' ]; then
            echo QA_UNCOMMITTED_GAME_CLEARED=1
        fi''')
    overlay = Newc()
    overlay.file('usr/bin/bench-init', init, 0o755)
    overlay.file('usr/bin/qa-session-log', '#!/bin/sh\nprintf "%s\\n" /dev/console\n', 0o755)
    binary = app / 'runtime/bin/sm64-prepare'
    runtime_libraries(B, overlay, [binary])
    overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
    overlay.file('etc/lilsmoke/input-maps/default.map',
                 (P / 'board/qemu-mipsel/input-maps/default.map').read_bytes())
    initrd = out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init',
        overlay_mib=16, root_device='/dev/sda', root_fstype=root_fstype)))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
        '-rtc', 'base=1970-01-01T00:00:00', '-kernel', str(B / 'images/vmlinux'),
        '-initrd', str(initrd), '-append', 'console=ttyS0 video=cirrusfb:640x480M-16@60 panic=-1',
        '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
        '-no-reboot', '-device', 'usb-kbd', '-qmp', f'unix:{out}/qmp.sock,server=on,wait=off',
        # The Malta fixture stranded virtio root reads under memory pressure.
        # IDE retains real block-backed SquashFS/FAT and physical RAM limits.
        '-drive', f'file={root_image},if=ide,index=0,media=disk,format=raw,snapshot=on',
        '-drive', f'file={disk},if=ide,index=1,media=disk,format=raw']

    def read(name):
        if name.startswith('preparation-') and name.endswith('.log'):
            number = name[len('preparation-'):-len('.log')]
            serial = read('serial.log')
            begin = 'QA_RUN_BEGIN_' + number + '\n'
            if begin not in serial: return ''
            return serial.split(begin, 1)[1].split('QA_PREPARATION_EXIT_' + number + '=', 1)[0]
        path = out / name
        return path.read_text(errors='replace') if path.exists() else ''

    qmp = None
    cancelled = past50 = game_seen = menu_sent = menu_resumed = checkpoint_cancelled = False
    menu_opened_at = None
    started = time.monotonic()
    with (out / 'serial.log').open('wb') as serial:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)
        try:
            while vm.poll() is None and 'reboot: System halted' not in read('serial.log'):
                first, second = read('preparation-1.log'), read('preparation-2.log')
                if qmp is None and (out / 'qmp.sock').exists():
                    qmp = Qmp(out / 'qmp.sock')
                cancel_token = 'SM64_STAGE compile_begin' if resume_mode else 'SM64_STAGE checkpoint_ready'
                if qmp and not cancelled and cancel_token in first:
                    qmp.call('screendump', dict(filename=str(out / 'assets-extracted.ppm')))
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data='alt')], **{'hold-time':1000}))
                    cancelled = True
                if (qmp and args.checkpoint_only and not checkpoint_cancelled and
                        re.search(r'build_progress completed=\d+ planned=\d+ phase=(?:Compiling|Assembly|Textures) target=', second)):
                    qmp.call('screendump', dict(filename=str(out / 'compilation-entered.ppm')))
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data='alt')], **{'hold-time': 1000}))
                    checkpoint_cancelled = True
                if qmp and not past50 and (('SM64_STAGE compile_done' in second) or any(
                        int(n) >= (1 if resume_mode else 200) for n in
                        re.findall(r'build_progress completed=(\d+)', second))):
                    qmp.call('screendump', dict(filename=str(out / 'past-50.ppm')))
                    past50 = True
                if qmp and not game_seen and 'OriginalFrame=600 ' in second:
                    qmp.call('screendump', dict(filename=str(out / 'game.ppm')))
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data='ret'),
                        dict(type='qcode', data='spc')], **{'hold-time':1500}))
                    game_seen = menu_sent = True
                if qmp and menu_sent and menu_opened_at is None and 'SM64_MENU opened' in second:
                    qmp.call('screendump', dict(filename=str(out / 'game-menu.ppm')))
                    menu_opened_at = time.monotonic()
                if (qmp and menu_opened_at is not None and not menu_resumed and
                        time.monotonic() - menu_opened_at >= 1.6):
                    qmp.call('send-key', dict(keys=[dict(type='qcode', data='alt')],
                                             **{'hold-time': 1000}))
                    menu_resumed = True
                if time.monotonic() - started > 7200:
                    raise RuntimeError('Full FAT32 clock-constrained VM timed out')
                time.sleep(0.5)
        finally:
            if qmp: qmp.close()
            if vm.poll() is None: vm.terminate()
            try: vm.wait(timeout=10)
            except subprocess.TimeoutExpired: vm.kill(); vm.wait()
    serial = read('serial.log')
    first, second, third = (read('preparation-' + str(n) + '.log') for n in (1, 2, 3))
    for number, log in enumerate((first, second, third), 1):
        (out / ('preparation-' + str(number) + '.log')).write_text(log)
    (out / 'process.log').write_text('\n'.join(line for line in serial.splitlines() if line.startswith('QA_PROCESS ')) + '\n')
    if args.checkpoint_only:
        assert all(token in serial for token in ('QA_PREPARATION_EXIT_1=130',
            'QA_PREPARATION_EXIT_2=130', 'QA_CANCEL_ORIGINAL_PRESERVED=1', 'QA_STORAGE_UMOUNT=0'))
        assert cancelled and checkpoint_cancelled
        assert 'SM64_STAGE assets_done' in first and 'SM64_STAGE checkpoint_ready' in first
        assert 'SM64_STAGE resume_assets' in second and 'SM64_STAGE unpack_begin' not in second
        assert 'SM64_STAGE plan_done' in second and 'SM64_STAGE compile_begin' in second
        assert 'SM64_STAGE compile_done' not in second
        assert not re.search(r'Out of memory|oom-kill|Killed process|virtual memory exhausted', serial, re.I)
        original = out / 'PRIVATE-preserved-original.z64'
        subprocess.run(['mcopy', '-o', '-i', str(disk), '::/Test App/original/Owner ROM.z64', str(original)], check=True)
        assert hashlib.sha256(original.read_bytes()).hexdigest() == owner_hash
        assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == owner_hash
        fsck = subprocess.run(['fsck.vfat', '-n', str(disk)], capture_output=True, text=True)
        (out / 'filesystem.log').write_text(fsck.stdout + fsck.stderr)
        assert fsck.returncode == 0
        epoch = int(re.search(r'QA_GUEST_EPOCH=(\d+)', serial).group(1))
        assert epoch < 315532800
        result = dict(passed=True, fresh_rom_extraction=True, guest_clock_epoch=epoch,
            planning_completed=True, native_compilation_started=True,
            completed_tasks_at_stop=max(map(int, re.findall(r'build_progress completed=(\d+)', second))),
            cancel_preserves_original=True, resumes_without_reextracting=True,
            clean_compile_checkpoint=True, storage_cleanly_unmounted=True, filesystem_clean=True,
            owner_rom_preserved=True, full_game_compiled=False,
            requires_separate_full_build_and_resume_acceptance=True,
            cpus=1, ram_mib=120, process_limit_kib=131072, physical_card_modified=False,
            runtime_sources={name: hashlib.sha256((args.runtime / name).read_bytes()).hexdigest()
                for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py')})
        (out / 'checkpoint-report.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result, indent=2))
        return
    if 'SM64_PREPARATION_ERROR' in serial:
        active_path = out / 'failure-active.json'
        copied = subprocess.run(['mcopy', '-o', '-i', str(disk),
            '::/Test App/.sm64-cache/active.json', str(active_path)], capture_output=True)
        if copied.returncode == 0:
            active = json.loads(active_path.read_text())['generation']
            if re.fullmatch(r'working-[a-z0-9_-]{1,70}', active):
                subprocess.run(['mcopy', '-o', '-i', str(disk),
                    '::/Test App/.sm64-cache/' + active + '/build.log',
                    str(out / 'failure-build.log')], capture_output=True)
    assert all(token in serial for token in ('QA_PREPARATION_EXIT_1=130', 'QA_PREPARATION_EXIT_2=0',
        'QA_PREPARATION_EXIT_3=0', 'QA_CANCEL_ORIGINAL_PRESERVED=1', 'QA_STORAGE_UMOUNT=0')), serial[-2000:]
    epoch = int(re.search(r'QA_GUEST_EPOCH=(\d+)', serial).group(1))
    assert epoch < 315532800
    assert int(re.search(r'MemTotal:\s+(\d+)', serial).group(1)) <= 112640
    assert int(re.search(r'SwapTotal:\s+(\d+)', serial).group(1)) == 0
    assert not re.search(r'Out of memory|oom-kill|Killed process|virtual memory exhausted', serial, re.I)
    assert ('SM64_STAGE resume_assets' if resume_mode else 'SM64_STAGE assets_done') in first
    assert 'SM64_STAGE resume_assets' in second
    assert 'SM64_STAGE unpack_begin' not in second
    assert 'SM64_STAGE compile_done' in second
    assert 'OriginalEngineTest=FINISHED Frames=900 Rendered=900' in second
    assert 'OriginalEngineTest=FINISHED Frames=30 Rendered=30' in third
    assert 'SM64_STAGE' not in third
    assert past50 and game_seen and 'SM64_MENU resumed' in second
    if resume_mode:
        assert 'QA_UNCOMMITTED_GAME_CLEARED=1' in serial
    assert second.count(' -o build/original-mips/level_rules.mk ') < 3
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest() == owner_hash
    record_path = out / 'current.json'
    subprocess.run(['mcopy', '-o', '-i', str(disk), '::/Test App/.sm64-cache/current.json', str(record_path)], check=True)
    record = json.loads(record_path.read_text())
    generation = record['generation']
    assert re.fullmatch(r'ready-[a-f0-9]{32}', generation)
    cooked = out / 'PRIVATE-cooked-game'
    subprocess.run(['mcopy', '-o', '-i', str(disk), '::/Test App/.sm64-cache/' + generation + '/game', str(cooked)], check=True)
    assert hashlib.sha256(cooked.read_bytes()).hexdigest() == record['game_sha256']
    assert subprocess.run(['mdir', '-i', str(disk), '::/Test App/original/Owner ROM.z64'], capture_output=True).returncode != 0
    assert subprocess.run(['mdir', '-i', str(disk), '::/Test App/.sm64-cache/' + generation + '/source'], capture_output=True).returncode != 0
    fsck = subprocess.run(['fsck.vfat', '-n', str(disk)], capture_output=True, text=True)
    (out / 'filesystem.log').write_text(fsck.stdout + fsck.stderr)
    assert fsck.returncode == 0
    if resume_mode:
        build_log = out / 'build.log'
        subprocess.run(['mcopy', '-o', '-i', str(disk),
            '::/Test App/.sm64-cache/' + generation + '/build.log', str(build_log)], check=True)
        text = build_log.read_text()
        for target in forced_targets:
            assert target in text, 'Selected native compile/link job was not executed: ' + target
    from PIL import Image
    for path in out.glob('*.ppm'):
        with Image.open(path) as frame: frame.save(path.with_suffix('.png'))
    result = dict(passed=True, full_native_mips_build=not resume_mode,
        complete_fat32_vm_preparation=not resume_mode, complete_fat32_vm_resume=resume_mode,
        production_extraction_ui=True, real_rom_extraction=True, guest_clock_epoch=epoch,
        no_system_host_clock_changes=True, source_future_date_loop_fixed=True,
        cancel_preserves_original=True, resumes_without_reextracting=True, real_build_progress=True,
        passed_50_percent=True, rendered_game_frames=900, native_menu_and_resume=True,
        repeat_launch=True, durable_game_readback=True, deletes_only_committed_app_original=True,
        game_sha256=record['game_sha256'],
        owner_rom_preserved=True, filesystem_clean=True, cpus=1, ram_mib=120,
        process_limit_kib=131072, application_filesystem='FAT32', full_game_compiled=not resume_mode,
        production_application_script=True, launcher_sha256=hashlib.sha256(launcher.encode()).hexdigest(),
        swap_kib=0,
        selected_vm_compile_and_link_targets=forced_targets,
        prior_vm_compilation=prior_compilation,
        covered_vm_compile_and_link_targets=sorted(set(forced_targets) |
            set(prior_compilation['completed_compiler_targets'] if prior_compilation else [])),
        verified_snapshot_game_sha256=capture['generated_elf_sha256'] if resume_mode else None,
        native_reference_dir=str(args.native) if resume_mode else None,
        private_snapshot_reference_dir=str(args.resume_snapshot) if resume_mode else None,
        uncommitted_final_output_recovery=resume_mode,
        fresh_clock_checkpoint=fresh if resume_mode else None,
        test_scope=('Full native build plus fresh-clock VM extraction/planning and VM resume, '
                    'selected compiler jobs, linking and game' if resume_mode else 'Full fresh VM preparation'),
        physical_hc16xx=False, runtime_sources={name: hashlib.sha256((args.runtime / name).read_bytes()).hexdigest()
        for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py')})
    result['qa_report_transport'] = ('serial; IDE block-backed ' + root_fstype +
        ' root and FAT application disk; no 9p or virtio')
    if prior_compilation:
        result['test_scope'] += '; six C jobs byte-verified from clean predecessor VM across ASM-only header fix'
    result['root_filesystem'] = root_fstype
    result['root_is_block_backed_readonly'] = True
    result['root_source_squashfs_sha256'] = hashlib.sha256((B / 'images/rootfs.squashfs').read_bytes()).hexdigest()
    (out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
