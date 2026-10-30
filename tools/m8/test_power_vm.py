#!/usr/bin/env python3
"""Terminal shutdown in a private MIPS VM. No physical drives are opened.

The QA-only executable permits Cirrus. Production only permits HC16xx hcfb.
"""
import gzip
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from PIL import Image

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')
QEMU = Path('/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel')
sys.path.insert(0, str(P/'tools/hwtest'))
from mips_boot_smoke import Newc

BOOT = '''#!/bin/sh
exec </dev/console >/dev/console 2>&1
/bin/busybox --install -s /bin
export PATH=/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mount -t tmpfs tmpfs /tmp
mkdir -p /media/card1 /media/card2 /aliases/library
mknod /dev/mmcblk0p1 b 8 0
mknod /dev/mmcblk0p2 b 8 16
mknod /dev/mmcblk0 b 8 0
mount -t vfat /dev/mmcblk0p1 /media/card1 || exit 1
mount -t vfat /dev/mmcblk0p2 /media/card2 || exit 1
mkdir -p /media/card1/cubegm/lib
mount --bind /media/card1/cubegm/lib /aliases/library || exit 1
cp /bin/busybox /media/card1/busybox
/media/card1/busybox sleep 600 &
(cd /media/card2; exec 3>pending-save; while :; do echo save >&3; sleep 1; done) &
cp /qa-guard /tmp/guard
echo poweroff >/tmp/lilsmoke-m8-shutdown
sleep 2
echo QA_BEGIN_TERMINAL
cat /proc/mounts
exec /tmp/guard --shutdown
'''

def checked(args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True, text=True).stdout

def main():
    out=Path(os.environ.get('LILSMOKE_POWER_QA_OUT', str(P/'test-results/shutdown-sm64-2026-10-10/power-vm')))
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='m8-power-vm-',dir='/root') as tmp:
        work=Path(tmp)
        src=P/'board/segam-m8-v6-stock'
        guard=work/'guard'
        checked([B/'host/bin/mipsel-buildroot-linux-musl-gcc','-static','-Os','-std=c11',
            '-DM8_POWER_QA','-I'+str(B/'staging/usr/include/SDL'),src/'m8-safe-power.c',
            src/'m8-power-ui.c',src/'m8-native-video.c','-o',guard])
        assert 'QA_ONLY' in checked(['qemu-mipsel','-cpu','74Kf',guard,'--self-check'])
        cards=[]
        for label in ('LILOS','LILROMS'):
            card=work/(label+'.fat')
            with card.open('wb') as f: f.truncate(64*1024*1024)
            checked(['mkfs.fat','-F','32','-n',label,card]);cards.append(card)
        archive=Newc()
        for name in ('bin','sbin','lib','etc','dev','proc','sys','tmp','media','aliases'):
            archive.dir(name)
        archive.chrdev('dev/console',5,1)
        archive.file('bin/busybox',(B/'target/bin/busybox').read_bytes(),0o755)
        archive.symlink('bin/sh','busybox')
        archive.file('lib/libc.so',(B/'target/lib/libc.so').read_bytes(),0o755)
        archive.symlink('lib/ld-musl-mipsel.so.1','libc.so')
        archive.file('init','#!/bin/sh\nexec /bin/busybox init\n',0o755)
        archive.file('etc/inittab','::sysinit:/boot.sh\n',0o644)
        archive.file('boot.sh',BOOT,0o755)
        archive.file('qa-guard',guard.read_bytes(),0o755)
        initrd=work/'init.cpio.gz';initrd.write_bytes(gzip.compress(archive.build(),compresslevel=1))
        sock=work/'qmp.sock'; log=work/'serial.log'
        command=[str(QEMU),'-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','128',
            '-kernel',str(B/'images/vmlinux'),'-initrd',str(initrd),'-append',
            'console=ttyS0 rdinit=/init video=cirrusfb:856x480M-24@60 panic=-1',
            '-drive',f'file={cards[0]},format=raw,if=ide,index=0',
            '-drive',f'file={cards[1]},format=raw,if=ide,index=1',
            '-vga','cirrus','-display','none','-serial',f'file:{log}',
            '-monitor','none','-no-reboot','-qmp',f'unix:{sock},server=on,wait=off']
        def qmp(name,args=None):
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as conn:
                conn.settimeout(10);conn.connect(str(sock))
                with conn.makefile('rwb',buffering=0) as f:
                    json.loads(f.readline())
                    for request in ({'execute':'qmp_capabilities'},{'execute':name,'arguments':args or {}}):
                        f.write(json.dumps(request).encode()+b'\n')
                        while True:
                            reply=json.loads(f.readline())
                            if 'error' in reply: raise RuntimeError(reply)
                            if 'return' in reply: break
        process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        snapshots=[]; shown=None
        try:
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                text=log.read_text(errors='replace') if log.exists() else ''
                if 'Power off operation succeeded\n' in text or 'Storage closed but SD needs repair\n' in text:
                    if shown is None: shown=time.monotonic()
                    elapsed=time.monotonic()-shown
                    wanted='success' if not snapshots else 'success-5s' if elapsed>=5 and len(snapshots)==1 else None
                    if wanted:
                        ppm=work/(wanted+'.ppm');qmp('screendump',{'filename':str(ppm)})
                        checked(['convert',ppm,out/(wanted+'.png')]);snapshots.append(wanted)
                if 'reboot: System halted' in text or 'reboot: Power down' in text:
                    ppm=work/'black.ppm';qmp('screendump',{'filename':str(ppm)})
                    checked(['convert',ppm,out/'black.png']);qmp('quit');process.wait(timeout=10);break
                if process.poll() is not None: break
                time.sleep(.05)
            assert process.poll() is not None,'terminal helper did not halt private VM'
            assert shown is not None,'success was never reached'
            elapsed=time.monotonic()-shown
            assert 9.5<=elapsed<=20,(elapsed,'success hold duration')
            text=log.read_text(errors='replace');(out/'serial.log').write_text(text)
            assert 'M8_POWER_BLACK hold_seconds=10' in text,text[-2500:]
            assert 'filesystem_clean=1' in text,text[-2500:]
            assert Image.open(out/'black.png').convert('RGB').getextrema()==((0,0),(0,0),(0,0))
            assert Image.open(out/'success.png').convert('RGB').tobytes()==Image.open(out/'success-5s.png').convert('RGB').tobytes()
            checks=[]
            for card in cards:
                result=subprocess.run(['fsck.fat','-n',str(card)],capture_output=True,text=True)
                (out/(card.stem+'-fsck.txt')).write_text(result.stdout+result.stderr)
                assert result.returncode==0,(card,result.stdout,result.stderr)
                checks.append(dict(volume=card.stem,fsck_readonly_exit=0,sha256=hashlib.sha256(card.read_bytes()).hexdigest()))
            (out/'result.json').write_text(json.dumps(dict(passed=True,cpu='MIPS single core',ram_mib=128,
                successful_hold_host_seconds=elapsed,qa_only_cirrus_adapter=True,physical_hcfb_test=False,
                mounted_library_alias=True,sd_mapped_executable=True,pending_save_writer=True,volumes=checks),indent=2)+'\n')
            print('PASS private MIPS terminal shutdown, 10s success screen and both FAT filesystems clean')
        finally:
            if process.poll() is None: process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait()
            if log.exists(): (out/'serial.log').write_bytes(log.read_bytes())
            error=process.stderr.read().decode(errors='replace');(out/'qemu-stderr.log').write_text(error)

if __name__=='__main__': main()
