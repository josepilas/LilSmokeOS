#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Test the shipped MIPS Python, shell and bootstrap, not Cache.current alone."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

HERE = Path(__file__).resolve().parent
P = HERE.parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')
sys.path.insert(0, str(P / 'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs, disk_devices
sys.path.insert(0, str(P / 'tools/portsmoke/sm64'))
from vm_scene_smoke import Qmp


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def selected(name):
    return name in ('SM64.sh', 'meta.json', 'icon.webp', 'SOURCE-OFFER.txt',
                    'runtime/recipe.json', 'runtime/native_build.py', 'runtime/cook.py',
                    'runtime/resume.py', 'runtime/build_progress.py', 'runtime/bin/sm64-prepare') or name.startswith(
                        ('runtime/python/', 'runtime/lib/'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'game', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=False)
    qualification = json.loads(a.package.with_name('package-report.json').read_text())
    handoff = json.loads(a.package.with_name('PRIVATE-prepared-update.json').read_text())
    assert sha(a.package) == qualification['sha256']
    assert sha(a.game) == handoff['replacement_game_sha256']
    report = dict(passed=False, cpus=1, ram_mib=120, gpu_3d=False, physical_hardware=False,
                  package_sha256=sha(a.package), game_sha256=sha(a.game), original_rom_required=False,
                  fixture='exact cached-launch subset; compiler/source inputs deliberately absent')
    with tempfile.TemporaryDirectory(prefix='sm64-cached-recipe-') as temporary:
        work = Path(temporary)
        bench = work / 'bench'
        bench.mkdir()
        storage = work / 'storage'
        app = storage / 'independent-apps/Super Mario 64'
        app.mkdir(parents=True)
        with zipfile.ZipFile(a.package) as archive:
            manifest = json.loads(archive.read('runtime/distribution-manifest.json'))['files']
            names = [name for name in archive.namelist() if selected(name)]
            for name in names:
                data = archive.read(name)
                assert name in manifest and hashlib.sha256(data).hexdigest() == manifest[name]['sha256']
                target = app / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                if name.startswith(('runtime/bin/', 'runtime/python/usr/bin/')) or name == 'SM64.sh':
                    target.chmod(0o755)
        report['exact_package_files'] = len(names)
        current = handoff['new_current']
        ready = app / '.sm64-cache' / current['generation']
        ready.mkdir(parents=True)
        shutil.copyfile(a.game, ready / 'game')
        (ready / 'game').chmod(0o755)
        (ready.parent / 'current.json').write_text(json.dumps(current) + '\n')
        (app / 'original').mkdir()
        (app / 'saves').mkdir()
        bad = storage / 'bad-recipe/runtime'
        bad.mkdir(parents=True)
        for name in ('native_build.py', 'cook.py', 'resume.py', 'build_progress.py'):
            shutil.copyfile(app / 'runtime' / name, bad / name)
        broken = json.loads((app / 'runtime/recipe.json').read_text())
        broken['version'] = 'Local-Development-0.0.3-perf.1'
        (bad / 'recipe.json').write_text(json.dumps(broken))
        before = sha(ready / 'game')
        overlay = Newc()
        overlay.file('etc/directfbrc', (P / 'board/qemu-mipsel/directfbrc').read_bytes())
        overlay.file('usr/bin/qa-session-log', '#!/bin/sh\nprintf "%s\\n" /bench/game.log\n', 0o755)
        init = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
mkdir -p /storage /bench /dev/pts /dev/shm
mount -t devpts devpts /dev/pts
mount -t tmpfs -o size=8m tmpfs /dev/shm
mount -t 9p -o trans=virtio,version=9p2000.L storage /storage || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L bench /bench || poweroff -f
export LILSMOKE_APP_DIR='/storage/independent-apps/Super Mario 64'
export LILSMOKE_APP_ID=org.lilsmoke.sm64 LILSMOKE_HCFB=0
export SDL_VIDEODRIVER=directfb SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export LILSMOKE_DFB_PIXEL_OPTIONS=depth=24,pixelformat=RGB24
export LILSMOKE_PRIVATE_HARDWARE_QA=1 SM64_ORIGINAL_TEST_FRAMES=120 SM64_ORIGINAL_RENDER_EVERY=1
export LILSMOKE_SESSION_LOG=/usr/bin/qa-session-log
export DFBARGS=no-hardware,no-cursor,no-sighandler,quiet,vt,no-vt-switch,no-vt-switching,disable-module=keyboard,linux-input-force,depth=24,pixelformat=RGB24
ulimit -v 131072
export PYTHONHOME="$LILSMOKE_APP_DIR/runtime/python/usr"
export LD_LIBRARY_PATH="$LILSMOKE_APP_DIR/runtime/lib:/usr/lib:/lib"
"$LILSMOKE_APP_DIR/runtime/python/usr/bin/python3.11" -u /storage/bad-recipe/runtime/native_build.py --app /storage/bad-recipe --cached-only >/bench/broken.log 2>&1
echo BROKEN_RECIPE_EXIT=$?
"$LILSMOKE_APP_DIR/runtime/python/usr/bin/python3.11" -u "$LILSMOKE_APP_DIR/runtime/native_build.py" --app "$LILSMOKE_APP_DIR" --cached-only >/bench/cache.log 2>&1
echo GOOD_CACHE_EXIT=$?
unset PYTHONHOME
for attempt in 1 2; do
    echo CACHED_LAUNCH_BEGIN=$attempt
    /bin/sh "$LILSMOKE_APP_DIR/SM64.sh"
    echo CACHED_LAUNCH_EXIT=$?
done
sync
echo CACHED_TEST_DONE
poweroff -f
'''
        overlay.file('usr/bin/bench-init', init, 0o755)
        boot = work / 'boot.cpio.gz'
        boot.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init', overlay_mib=16, trace=True)))
        devices = disk_devices(B, [('storage', storage, False), ('bench', bench, False)])
        devices = [x.replace(str(B / 'images/rootfs.squashfs'),
                   '/root/lilsmoke-media-c45-final-video/rootfs.squashfs') for x in devices]
        socket = work / 'qmp.sock'
        command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
                   '-M', 'malta', '-cpu', '74Kf', '-smp', '1', '-m', '120', '-kernel', str(B / 'images/vmlinux'),
                   '-initrd', str(boot), '-append', 'console=ttyS0 video=cirrusfb:640x480M-24@60 panic=-1',
                   '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
                   '-qmp', f'unix:{socket},server=on,wait=off', *devices]
        vm = qmp = None
        def read(name):
            path = bench / name
            return path.read_text(errors='replace') if path.exists() else ''
        try:
            with (bench / 'serial.log').open('wb') as output:
                vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 240
                captured = False
                while vm.poll() is None and 'CACHED_TEST_DONE' not in read('serial.log'):
                    text = read('game.log')
                    if not qmp and socket.exists():
                        qmp = Qmp(socket)
                    if qmp and not captured and 'SM64_PROFILE' in text:
                        from PIL import Image
                        image = work / 'game.ppm'
                        qmp.call('screendump', {'filename': str(image)})
                        im = Image.open(image)
                        if len(im.getcolors(im.width * im.height) or []) > 40:
                            im.save(bench / 'cached-game.png')
                            captured = True
                    if time.monotonic() > deadline:
                        raise RuntimeError('Cached production launch timed out: ' + read('serial.log')[-1500:] + text[-3000:])
                    time.sleep(.2)
            text = read('serial.log')
            game_log = read('game.log')
            assert 'BROKEN_RECIPE_EXIT=1' in text and 'Unsupported bundled build recipe' in read('broken.log')
            assert 'GOOD_CACHE_EXIT=0' in text and '/storage/independent-apps/Super Mario 64/.sm64-cache/' in read('cache.log')
            assert text.count('CACHED_LAUNCH_EXIT=0') == 2, game_log[-4000:]
            assert game_log.count('SM64_HANDOFF cached=1 preparation_skipped=1') == 2
            assert game_log.count('SM64_GAME exit=0 signal=0 cached_hash_verified=1') == 2
            assert game_log.count('OriginalEngineTest=FINISHED Frames=120 Rendered=120') == 2, game_log[-4000:]
            assert 'NativeLoadingIcon' not in game_log and 'compile_begin' not in game_log and 'unpack_begin' not in game_log
            assert sha(ready / 'game') == before and not any((app / 'original').iterdir())
            report.update(passed=True, actual_bundled_python=True, actual_shell_and_bootstrap=True,
                          broken_recipe_reproduced=True, cached_launches=2, frames_per_launch=120,
                          extraction_ui_skipped=True, game_unchanged=True, no_recompile=True,
                          screenshot=captured, physical_audio_not_tested=True)
        except Exception as error:
            report['error'] = str(error)
            raise
        finally:
            if qmp:
                qmp.close()
            if vm and vm.poll() is None:
                vm.terminate()
            if vm:
                try:
                    vm.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    vm.kill(); vm.wait()
            for path in bench.iterdir():
                if path.is_file():
                    shutil.copyfile(path, a.out / path.name)
            (a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS shipped MIPS Python recipe check and two actual cached SM64 launches; no compilation or ROM required')


if __name__ == '__main__':
    main()
