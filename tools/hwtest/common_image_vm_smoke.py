#!/usr/bin/env python3
"""Exercise the actual two-partition image in MIPS Linux. Never flash hardware.

QEMU supplies a diagnostic pre-kernel loader; this does not certify M8 boot.
Only an isolated disposable image receives a QA key/profile and personal ROM.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from nacl.signing import SigningKey
from mips_boot_smoke import Newc
from mips_disk_fixture import overlay_initramfs

ROOT = Path(__file__).resolve().parents[2]

def run(args):
    result = subprocess.run(list(map(str, args)), text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{args}: exit {result.returncode}\n{result.stdout}{result.stderr}')
    return result.stdout

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def main():
    def interrupted(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, interrupted)
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, default=Path('/root/lilsmoke-os/build/buildroot/output'))
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--qemu', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--snes-index', type=int, default=0,
                   help='SNES index in the system menu when optional ports are also installed')
    a = p.parse_args()
    if not 0 <= a.snes_index < 32:
        p.error('--snes-index must be between 0 and 31')
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    events = []
    command = []
    initial_hash = sha(a.image)
    with tempfile.TemporaryDirectory(prefix='lilsmoke-img-qa-', dir='/root') as directory:
        work = Path(directory)
        image = work / 'card.img'
        shutil.copyfile(a.image, image)
        with image.open('r+b') as f:
            f.truncate(896 * 1024 * 1024)
        key = SigningKey.generate()
        private = work / 'private.key'
        private.write_bytes(bytes(key))
        private.chmod(0o600)
        identity = {}
        for line in (a.build / 'target/etc/lilsmoke/syspck-policy.conf').read_text().splitlines():
            k, v = line.split('=', 1)
            identity[k] = v
        fields = {k: identity[k] for k in ('Board', 'HardwareRevision', 'SoC', 'Architecture', 'ABI', 'Libc', 'KernelID')}
        fields.update(FormatVersion='1', PackageID='image-qa-tv', Version='1.0', DTBinding='none',
                      Kind='userspace', RequiredRAMMB='128', KeyID='image-qa')
        profile = work / 'profile.ini'
        profile.write_text('[SystemPackage]\n' + ''.join(k+'='+v+'\n' for k, v in fields.items()))
        config = work / 'tv.conf'
        config.write_text('DEVICE_MODE=tv\nTV_SAFE_AREA_PERCENT=5\nSHOW_BATTERY=off\n')
        package = work / 'main.syspck'
        run(['python3', ROOT/'tools/syspck/make_syspck.py', 'pack', '--profile', profile,
             '--config', config, '--private', private, '--output', package])
        loop = run(['losetup', '--find', '--show', '--partscan', image]).strip()
        mount = work / 'mount'
        mount.mkdir()
        try:
            run(['mount', loop+'p1', mount])
            try:
                assert sha(mount/'lilsmoke/slots/A/rootfs.squashfs') == sha(a.build/'images/rootfs.squashfs')
                assert sha(mount/'lilsmoke/slots/A/vmlinux') == sha(a.build/'images/vmlinux')
                shutil.copyfile(package, mount/'main.syspck')
            finally:
                run(['umount', mount])
            run(['mount', loop+'p2', mount])
            try:
                romdir = mount / 'roms/snes'
                romdir.mkdir(parents=True)
                shutil.copyfile(a.archive, romdir/'SMW-test.7z')
                (mount/'first-boot-file.txt').write_text('retain this across expansion\n')
            finally:
                run(['umount', mount])
        finally:
            run(['losetup', '-d', loop])
        fixture = Newc()
        fixture.file('etc/lilsmoke/syspck-keys/image-qa.pub', bytes(key.verify_key))
        defaults = (a.build/'target/etc/lilsmoke/lilsmoke.conf.default').read_text()
        fixture.file('etc/lilsmoke/lilsmoke.conf.default', defaults + '\nSNES_EMULATOR=pocketsnes\n')
        fixture.file('etc/init.d/S99image-qa', '''#!/bin/sh
case "$1" in start)
echo IMAGE_STORAGE_REPORT
cat /run/lilsmoke-storage/status
/usr/lib/lilsmoke/lilsmoke-disk info /dev/vda
blkid /dev/vda1 /dev/vda2
cat /proc/meminfo
cat /proc/mounts
cat /storage/first-boot-file.txt
(
while ! grep -q 'UI: entering main loop' /persist/logs/lilsmoke-ui.log; do sleep 1; done
echo IMAGE_UI_READY
while ! pidof pocketsnes >/dev/null; do sleep 1; done
echo IMAGE_GAME_RUNNING
cat /persist/config/lilsmoke.conf
) &
;; esac
''', 0o755)
        initrd = work/'bootstrap.cpio.gz'
        initrd.write_bytes(gzip.compress(overlay_initramfs(a.build, fixture, sdcard=True)))
        sock = work/'qmp.sock'
        command = [str(a.qemu), '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '128',
                   '-accel', 'tcg,thread=single', '-kernel', str(a.build/'images/vmlinux'),
                   '-initrd', str(initrd), '-append', 'console=ttyS0 video=cirrusfb:856x480M-24@60 panic=-1',
                   '-vga', 'cirrus', '-display', 'none', '-serial', 'stdio', '-no-reboot', '-device', 'usb-kbd',
                   '-qmp', f'unix:{sock},server=on,wait=off',
                   '-audiodev', 'driver=none,id=audio', '-device', 'AC97,audiodev=audio',
                   '-drive', f'file={image},if=none,id=sd,format=raw',
                   '-device', 'virtio-blk-pci,drive=sd,addr=0x14.0,multifunction=on']
        def qmp(name, args=None):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.settimeout(30)
                s.connect(str(sock))
                with s.makefile('rwb', buffering=0) as f:
                    json.loads(f.readline())
                    for request in ({'execute': 'qmp_capabilities'}, {'execute': name, 'arguments': args or {}}):
                        f.write(json.dumps(request).encode()+b'\n')
                        while True:
                            reply = json.loads(f.readline())
                            if 'error' in reply: raise RuntimeError(reply)
                            if 'return' in reply: break
                    return reply['return']
        started = time.monotonic()
        def key(name, hold=180):
            qmp('human-monitor-command', {'command-line': f'sendkey {name} {hold}'})
            events.append({'key': name, 'hold_ms': hold, 'seconds': time.monotonic()-started})
            time.sleep(hold/1000+1)
        def capture(name):
            ppm, png = out/(name+'.ppm'), out/(name+'.png')
            qmp('screendump', {'filename': str(ppm)})
            run(['convert', ppm, png])
            ppm.unlink()
        logpath = work/'serial.log'
        with logpath.open('wb') as log:
            vm = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
            try:
                def wait(needle, timeout=300):
                    end = time.monotonic()+timeout
                    while needle not in logpath.read_text(errors='replace'):
                        current = logpath.read_text(errors='replace')
                        if 'Kernel panic' in current or 'Storage unavailable' in current:
                            raise RuntimeError('guest boot failed; inspect serial.log')
                        if vm.poll() is not None or time.monotonic()>end:
                            raise RuntimeError('guest did not reach '+needle)
                        time.sleep(1)
                wait('IMAGE_UI_READY')
                time.sleep(3)
                capture('01-tv-system')
                for _ in range(a.snes_index):
                    key('right')
                key('ret')
                time.sleep(3)
                capture('02-snes-list')
                key('ret')
                wait('IMAGE_GAME_RUNNING')
                time.sleep(22)
                capture('03-snes-running')
                key('ret', 650)
                time.sleep(12)
                capture('04-start-button')
                # Write only to this QEMU serial console, never a desktop window.
                vm.stdin.write(b'poweroff\n')
                vm.stdin.flush()
                # Malta halts its CPU without necessarily closing QEMU. Wait
                # for Linux's shutdown before releasing the writable image.
                wait('reboot: System halted', timeout=45)
                qmp('quit')
                vm.wait(timeout=15)
            finally:
                if vm.poll() is None: vm.terminate()
                try: vm.wait(timeout=15)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait()
                log.flush()
                shutil.copyfile(logpath, out/'serial.log')
        loop = run(['losetup', '--find', '--show', '--partscan', image]).strip()
        try:
            run(['fsck.vfat', '-n', loop+'p1'])
            run(['fsck.exfat', '-n', loop+'p2'])
            run(['mount', '-o', 'ro', loop+'p2', mount])
            try:
                assert (mount/'first-boot-file.txt').read_text() == 'retain this across expansion\n'
                assert sha(mount/'roms/snes/SMW-test.7z') == sha(a.archive)
                stat = os.statvfs(mount)
                assert stat.f_blocks*stat.f_frsize > 375*1024*1024
            finally: run(['umount', mount])
            run(['mount', '-o', 'ro', loop+'p1', mount])
            try:
                assert (mount/'lilsmoke/grown').read_text().strip() == 'complete'
                assert not (mount/'lilsmoke/grow/journal').exists()
                persist = work/'persist'
                persist.mkdir()
                metadata = run(['dumpe2fs', '-h', mount/'lilsmoke/persist.ext4'])
                (out/'persistence-metadata.txt').write_text(metadata)
                assert 'needs_recovery' not in metadata, 'persistence journal was not closed during shutdown'
                run(['e2fsck', '-fn', mount/'lilsmoke/persist.ext4'])
                run(['mount', '-o', 'loop,ro', mount/'lilsmoke/persist.ext4', persist])
                try:
                    live = (persist/'config/lilsmoke.conf').read_text()
                    assert 'DEVICE_MODE=tv' in live and 'TV_SAFE_AREA_PERCENT=5' in live
                    assert (persist/'syspck/boot-selected').read_text().strip() == sha(package)
                    shutil.copyfile(persist/'logs/lilsmoke-ui.log', out/'ui.log')
                finally: run(['umount', persist])
            finally: run(['umount', mount])
        finally: run(['losetup', '-d', loop])
    assert sha(a.image) == initial_hash
    report = {'passed': True, 'ram_mb': 128, 'cpu_count': 1, 'command': command,
              'events': events, 'source_img_sha256': initial_hash, 'kernel_sha256': sha(a.build/'images/vmlinux'),
              'rootfs_sha256': sha(a.build/'images/rootfs.squashfs'),
              'checks': ['guest first-boot exFAT expansion', 'original files retained', 'signed main.syspck selected',
                         'TV UI opened', 'SNES selected through UI', 'PocketSNES process started from 7z',
                         'Start input sent through QMP', 'guest shutdown completed',
                         'FAT32/exFAT/ext4 checks clean without repair', 'persistence journal closed',
                         'source image unchanged'],
              'limits': 'Malta/Cirrus/AC97 substitutes. Diagnostic external loader. Not physical M8 boot or FPS evidence.'}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print('COMMON_IMAGE_VM_OK', flush=True)

if __name__ == '__main__': main()
