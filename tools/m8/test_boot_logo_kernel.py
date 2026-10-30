#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Run the production logo preload under a real emulated MIPS Linux kernel."""
import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]
B = Path('/root/lilsmoke-os/build/buildroot/output')
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--artifacts', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--display', type=Path)
args = ap.parse_args()
out = args.out.resolve()
out.mkdir(exist_ok=False)
sys.path.insert(0, str(P / 'tools/hwtest'))
from mips_boot_smoke import Newc
from mips_disk_fixture import runtime_libraries

init = '''#!/bin/sh
export PATH=/bin:/usr/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs dev /dev
mkdir -p /run
export M8_TEST_FB=/run/fb M8_TEST_STATE=/run/state
export LILSMOKE_HCFB=1
fail=0
for mode in native alias sdl update rects2 flip rect rects black exec reject-fixed; do
    cp /qa/seed /run/fb
    start=$(awk '{printf "%.0f", $1 * 1000}' /proc/uptime)
    [ "$start" -gt 0 ] || start=1
    printf 'M8_BOOT_LOGO_V1 %s\n' "$start" >/run/state
    chmod 600 /run/state
    if [ "$mode" = exec ]; then export LILSMOKE_M8_LOADING_GAME=1; else unset LILSMOKE_M8_LOADING_GAME; fi
    if [ "$mode" = reject-fixed ]; then
        export M8_TEST_REJECT_FIXED=1
        probe=native
    else
        unset M8_TEST_REJECT_FIXED
        probe=$mode
    fi
    LD_PRELOAD=/qa/gate.so:/qa/display.so:/qa/fake.so \
        LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state /qa/probe "$probe"
    status=$?
    if [ "$mode" = reject-fixed ]; then expected=70; else expected=0; fi
    if [ "$status" = "$expected" ] && [ ! -e /run/state ]; then
        echo "QA_BOOT_KERNEL mode=$mode exit=$status PASS"
    else
        echo "QA_BOOT_KERNEL mode=$mode exit=$status FAIL"
        fail=1
    fi
done
unset M8_TEST_REJECT_FIXED
export LILSMOKE_M8_NATIVE_VIDEO=1 LILSMOKE_M8_LOADING_GAME=1
for mode in rgb16 rgb32 menu16 menu32 flip; do
    cp /qa/seed /run/fb
    start=$(awk '{printf "%.0f", $1 * 1000}' /proc/uptime)
    [ "$start" -gt 0 ] || start=1
    printf 'M8_BOOT_LOGO_V1 %s\n' "$start" >/run/state
    chmod 600 /run/state
    LD_PRELOAD=/qa/gate.so:/qa/display.so:/qa/fake.so \
        LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state /qa/native-probe "$mode"
    status=$?
    if [ "$status" = 0 ]; then
        echo "QA_NATIVE_LOADING mode=$mode PASS"
    else
        fail=1
    fi
done
unset LILSMOKE_M8_NATIVE_VIDEO LILSMOKE_M8_LOADING_GAME
cp /qa/seed /run/fb
rm -f /run/state
unset M8_TEST_REJECT_FIXED
mkdir -p /run/m8-loading
touch /run/m8-loading/active
printf '70\tStarting emulator\n' > /run/m8-loading/progress
LD_PRELOAD=/qa/fake.so /qa/show --show /qa/logo.argb /run/state --hold /run/m8-loading --no-minimum &
loader=$!
tries=0
while [ ! -s /run/state ] && [ "$tries" -lt 40 ]; do sleep 0.05; tries=$((tries+1)); done
LD_PRELOAD=/qa/gate.so:/qa/display.so:/qa/fake.so LILSMOKE_M8_LOADING_GAME=1 LILSMOKE_M8_LOADING_DIR=/run/m8-loading LILSMOKE_M8_BOOT_STATE=/run/m8-boot-logo.state /qa/probe exec
status=$?
rm -f /run/m8-loading/active
wait "$loader"
if [ "$status" = 0 ] && [ -f /run/m8-loading/stopped ]; then echo QA_LIVE_LOADING_PASS; else fail=1; fi
rm -f /run/state
export M8_TEST_STATE=/run/m8-boot-logo.state
LD_PRELOAD=/qa/display.so:/qa/fake.so /bin/sh /usr/lib/lilsmoke/m8-loading-run /qa/probe exec
status=$?
if [ "$status" = 0 ] && [ -f /run/m8-loading/stopped ] && [ ! -e /run/m8-boot-logo.state ]; then
    echo QA_BUSYBOX_LOADING_WRAPPER_PASS
else
    fail=1
fi
export M8_TEST_STATE=/run/state
LD_PRELOAD=/qa/fake.so M8_TEST_REQUIRE_MODE_AND_PAN=1 M8_TEST_NO_ALPHA=1 /qa/show --show /qa/logo.argb /run/state
status=$?
echo "QA_BOOT_HELPER_EXIT=$status"
[ "$status" = 0 ] || fail=1
echo "QA_BOOT_KERNEL_FAILURES=$fail"
sync
poweroff -f
'''
root = Newc()
for directory in ('bin', 'usr', 'usr/bin', 'usr/lib', 'usr/lib/lilsmoke', 'os', 'dev', 'proc', 'sys', 'run', 'qa', 'lib', 'tmp'):
    root.dir(directory)
root.chrdev('dev/console', 5, 1)
root.file('bin/busybox', (B / 'target/bin/busybox').read_bytes(), 0o755)
for library in ('libc.so', 'ld-musl-mipsel.so.1'):
    root.file('lib/' + library, (B / 'target/lib' / library).read_bytes(), 0o755)
for applet in ('sh', 'mount', 'mkdir', 'cp', 'rm', 'awk', 'chmod', 'sync', 'poweroff', 'sleep', 'touch'):
    root.symlink('bin/' + applet, 'busybox')
root.file('init', init, 0o755)
root.file('qa/seed', b'\x55' * 256)
root.file('qa/logo.argb', b'M8ARGB1\0' + struct.pack('<II',8,4) + bytes((120,220,40,255))*32)
root.file('os/boot-logo.argb', b'M8ARGB1\0' + struct.pack('<II',8,4) + bytes((120,220,40,255))*32)
root.file('os/libm8-boot-logo.so', (args.artifacts / 'gate.so').read_bytes(), 0o755)
# Only the hardware syscall fixture is substituted; the production BusyBox
# launch wrapper, token protocol, preload chain and guest execs run unchanged.
root.file('os/m8-boot-logo', '#!/bin/sh\nLD_PRELOAD=/qa/fake.so exec /qa/show "$@"\n', 0o755)
root.file('usr/lib/lilsmoke/m8-loading-run', (P / 'package/lilsmoke-scripts/src/m8-loading-run').read_bytes(), 0o755)
binaries = [B / 'target/bin/busybox']
for name in ('gate.so', 'display.so', 'fake.so', 'show'):
    binary = args.display if name == 'display.so' and args.display else args.artifacts / name
    root.file('qa/' + name, binary.read_bytes(), 0o755)
    binaries.append(binary)
subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc'), '-O2',
    str(P / 'tools/m8/test_boot_logo_preload.c'), '-L' + str(args.artifacts),
    '-Wl,-rpath,/qa', '-l:fake.so', '-ldl', '-o', str(out / 'probe')], check=True)
root.file('qa/probe', (out / 'probe').read_bytes(), 0o755)
subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc'), '-O2',
    '-I' + str(B / 'staging/usr/include/SDL'),
    str(P / 'tools/m8/test_boot_logo_native.c'), '-L' + str(args.artifacts),
    '-Wl,-rpath,/qa', '-l:fake.so', '-ldl', '-o', str(out / 'native-probe')], check=True)
root.file('qa/native-probe', (out / 'native-probe').read_bytes(), 0o755)
runtime_libraries(B, root, binaries)
(out / 'boot.cpio.gz').write_bytes(gzip.compress(root.build()))
command = ['/root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel',
    '-M', 'malta', '-cpu', '74Kf-lilsmoke', '-smp', '1', '-m', '120',
    '-kernel', str(B / 'images/vmlinux'), '-initrd', str(out / 'boot.cpio.gz'),
    '-append', 'console=ttyS0 panic=-1', '-display', 'none', '-serial', 'stdio',
    '-monitor', 'none', '-no-reboot']
with (out / 'serial.log').open('wb') as log:
    vm = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 100
        while vm.poll() is None:
            text = (out / 'serial.log').read_text(errors='replace')
            if 'reboot: System halted' in text:
                break
            if time.monotonic() > deadline:
                raise RuntimeError('MIPS kernel logo regression timed out: ' + str(out))
            time.sleep(.2)
    finally:
        if vm.poll() is None:
            vm.terminate()
        vm.wait(timeout=10)
text = (out / 'serial.log').read_text(errors='replace')
for mode in ('native', 'alias', 'sdl', 'update', 'rects2', 'flip', 'rect', 'rects', 'black', 'exec', 'reject-fixed'):
    expected = 70 if mode == 'reject-fixed' else 0
    assert f'QA_BOOT_KERNEL mode={mode} exit={expected} PASS' in text, text[-5000:]
assert 'QA_BOOT_KERNEL_FAILURES=0' in text, text[-5000:]
for mode in ('rgb16', 'rgb32', 'menu16', 'menu32', 'flip'):
    assert f'QA_NATIVE_LOADING mode={mode} PASS' in text, text[-7000:]
assert 'M8NativeVideo fallback' not in text, text[-7000:]
assert 'QA_LIVE_LOADING_PASS' in text, text[-5000:]
assert 'QA_BUSYBOX_LOADING_WRAPPER_PASS' in text, text[-5000:]
assert 'QA_BOOT_HELPER_EXIT=0' in text and 'alpha=24/8 mode_committed=1 pan_committed=1' in text, text[-5000:]
assert 'M8BootLogo minimum_hold_complete elapsed_ms=' in text, text[-5000:]
report = dict(passed=True, actual_mips_linux_kernel=True, cpus=1, ram_mib=120,
    game_exec_chain=True, live_helper_acknowledgement=True, legacy_sdl_first_frame=True,
    blank_initialization_frames=True, busybox_loading_wrapper=True,
    native_first_frame=True, aliased_source_preserved=True, sdl_first_frame=True,
    native_rgb565_and_rgb32=True, native_menu_rgba4444=True,
    native_second_mapping=True, native_gameplay_frames_per_case=1000,
    shared_fixed_mapping=True, injected_einval_consumes_token=True,
    mode_pan_required=True, opaque_argb_required=True, helper_mapping_held_six_seconds=True,
    physical_hcfb_tested=False, physical_console_boot_tested=False,
    display_sha256=hashlib.sha256((args.display or args.artifacts / 'display.so').read_bytes()).hexdigest(),
    gate_sha256=hashlib.sha256((args.artifacts / 'gate.so').read_bytes()).hexdigest())
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
