#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise installed FFmpeg tools in a one-CPU, 120 MiB MIPS VM."""
import argparse
import gzip
import json
from pathlib import Path
import re
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--media', type=Path, required=True)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    acceptance = json.loads((args.media / 'report.json').read_text())
    if acceptance.get('passed') is not True:
        raise ValueError('Passed generated-media CLI fixtures required')
    args.out.mkdir(exist_ok=False)
    sys.path.insert(0, str(P / 'tools/hwtest'))
    from mips_boot_smoke import Newc
    from mips_disk_fixture import overlay_initramfs, runtime_libraries
    overlay = Newc()
    for name in ('ffmpeg', 'ffprobe'):
        overlay.file('usr/bin/' + name, (B / 'target/usr/bin' / name).read_bytes(), 0o755)
    runtime_libraries(B, overlay, [B / 'target/usr/bin/ffmpeg', B / 'target/usr/bin/ffprobe'])
    for name in ('signal.wav', 'pattern.rgb', 'broken.mp4'):
        overlay.file('qa/' + name, (args.media / name).read_bytes())
    init = """#!/bin/sh
exec </dev/console >/dev/console 2>&1
set -eu
trap 'echo FFMPEG_VM_FAILED; poweroff -f' EXIT
cat /proc/meminfo
ulimit -v 98304
/usr/bin/ffmpeg -nostdin -v error -threads 1 -filter_threads 1 -i /qa/signal.wav -c:a flac /tmp/signal.flac
/usr/bin/ffmpeg -nostdin -v error -threads 1 -filter_threads 1 -i /tmp/signal.flac -f s16le /tmp/recovered.pcm
echo FFMPEG_PCM_BEGIN
sha256sum /tmp/recovered.pcm
/usr/bin/ffmpeg -nostdin -v error -threads 1 -filter_threads 1 -i /qa/signal.wav -af volume=0.5,aresample=22050 /tmp/filtered.wav
/usr/bin/ffprobe -v error -show_entries stream=sample_rate -of default=noprint_wrappers=1 /tmp/filtered.wav
/usr/bin/ffmpeg -nostdin -v error -threads 1 -filter_threads 1 -f rawvideo -pixel_format rgb24 -video_size 64x48 -framerate 8 -i /qa/pattern.rgb -c:v mpeg4 -q:v 2 /tmp/pattern.mp4
/usr/bin/ffprobe -v error -count_frames -show_entries stream=width,height,nb_read_frames -of default=noprint_wrappers=1 /tmp/pattern.mp4
if /usr/bin/ffprobe -v error /qa/broken.mp4; then exit 1; fi
echo FFMPEG_VM_RESULT=PASS
trap - EXIT
poweroff -f
"""
    overlay.file('usr/bin/bench-init', init, 0o755)
    initrd = args.out / 'boot.cpio.gz'
    initrd.write_bytes(gzip.compress(overlay_initramfs(B, overlay, '/usr/bin/bench-init',
        overlay_mib=32, root_device='/dev/sda', root_fstype='ext2')))
    command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
        '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
        '-kernel', str(B / 'images/vmlinux'), '-initrd', str(initrd),
        '-append', 'console=ttyS0 panic=-1', '-display', 'none', '-serial', 'stdio',
        '-monitor', 'none', '-no-reboot', '-drive',
        'file=' + str(args.rootfs) + ',if=ide,index=0,media=disk,format=raw,snapshot=on']
    log = args.out / 'serial.log'
    with log.open('wb') as serial:
        vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=serial, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 180
            while vm.poll() is None:
                text = log.read_text(errors='replace')
                if 'reboot: System halted' in text:
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError('FFmpeg VM timed out')
                time.sleep(.2)
        finally:
            if vm.poll() is None:
                vm.terminate()
            try:
                vm.wait(timeout=10)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
    text = log.read_text(errors='replace')
    assert 'FFMPEG_VM_RESULT=PASS' in text, text[-6000:]
    assert acceptance['pcm_sha256'] in text
    for item in ('sample_rate=22050', 'width=64', 'height=48', 'nb_read_frames=8'):
        assert item in text, item
    assert int(re.search(r'SwapTotal:\s+(\d+)', text).group(1)) == 0
    assert not re.search(r'Out of memory|oom-kill|Killed process', text, re.I)
    result = dict(passed=True, cpus=1, physical_ram_mib=120, swap_kib=0,
        virtual_limit_kib=98304, audio_roundtrip=True, video_encode_probe=True,
        software_audio_filters=True, corrupt_input_rejected=True, physical_hc16xx=False)
    (args.out / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

