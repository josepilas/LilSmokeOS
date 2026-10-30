#!/usr/bin/env python3
"""Load the isolated firmware ABI in real Linux/MIPS, not host ABI substitution."""
import argparse
import gzip
import json
from pathlib import Path
import subprocess
import sys
import time

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--media',type=Path,help='Optional local MP4 fixture: exercise real firmware demux/create, not only dlopen')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(P/'tools/hwtest'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs
    overlay=Newc()
    for file in args.runtime.iterdir():
        if file.name in ('hcplayer-worker','ld.so.1') or '.so' in file.name:
            overlay.file('usr/lib/lilsmoke/hcplayer/'+file.name,file.read_bytes(),0o755)
    media_probe=''
    if args.media:
        if not args.media.is_file() or args.media.stat().st_size>2*1024*1024:
            raise ValueError('Media probe requires a local fixture no larger than 2 MiB')
        overlay.file('tmp/media-probe.mp4',args.media.read_bytes(),0o644)
        media_probe='''
"$root/ld.so.1" --library-path "$root" "$root/hcplayer-worker" /tmp/media-probe.mp4 </dev/null
echo HC_MEDIA_CREATE_EXIT=$?
'''
    overlay.file('usr/bin/bench-init','''#!/bin/sh
exec </dev/console >/dev/console 2>&1
cat /proc/meminfo
ulimit -v 65536
unset LD_PRELOAD LD_LIBRARY_PATH
root=/usr/lib/lilsmoke/hcplayer
"$root/ld.so.1" --library-path "$root" "$root/hcplayer-worker" --probe
echo HC_PROBE_EXIT=$?
'''+media_probe+'''
poweroff -f
''',0o755)
    initrd=args.out/'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B,overlay,'/usr/bin/bench-init',overlay_mib=16,
        root_device='/dev/sda',root_fstype='squashfs')))
    command=['nice','-n','10','taskset','-c','0','/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M','malta','-cpu','74Kf-lilsmoke','-smp','1','-m','120','-kernel',str(B/'images/vmlinux'),
        '-initrd',str(initrd),'-append','console=ttyS0 panic=-1','-display','none','-serial','stdio',
        '-monitor','none','-no-reboot','-nic','none','-drive',
        'file='+str(B/'images/rootfs.squashfs')+',if=ide,index=0,format=raw,snapshot=on']
    with (args.out/'serial.log').open('wb') as log:
        vm=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+120
            while vm.poll() is None:
                text=(args.out/'serial.log').read_text(errors='replace')
                if 'reboot: System halted' in text:break
                if time.monotonic()>deadline:raise TimeoutError('native ABI worker Linux probe')
                time.sleep(.2)
        finally:
            if vm.poll() is None:vm.terminate()
            try:vm.wait(timeout=10)
            except subprocess.TimeoutExpired:vm.kill();vm.wait()
    text=(args.out/'serial.log').read_text(errors='replace')
    assert 'HC_PROBE_EXIT=0' in text and 'HC_RUNTIME_LOADED' in text,text[-5000:]
    if args.media:
        assert 'HC_MEDIA_CREATE_EXIT=' in text,text[-5000:]
    (args.out/'report.json').write_text(json.dumps(dict(passed=True,cpus=1,ram_mib=120,
        address_limit_mib=64,native_firmware_api_loaded=True,physical_decoder_tested=False,
        media_create_exercised=bool(args.media),
        firmware_demux_index_failed='build index failed' in text),indent=2)+'\n')
    print('PASS isolated HC worker Linux/MIPS load, not physical decoder:',args.out)

if __name__=='__main__':main()
