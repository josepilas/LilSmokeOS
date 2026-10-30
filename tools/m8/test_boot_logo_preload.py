#!/usr/bin/env python3
"""Run production preload DSOs against a file-backed hcfb, not a device."""
import argparse
import hashlib
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--payload', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
board = root / 'board/segam-m8-v6-stock'
a.out.mkdir()

def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)

for arch, compiler in [('host', 'cc'), ('mips', a.sdk / 'host/bin/mipsel-buildroot-linux-musl-gcc')]:
    dest = a.out / arch
    dest.mkdir()
    common = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    if arch == 'mips':
        common += ['-mips32r2', '-mtune=74kc', '-mdspr2', '-mfp32', '-mhard-float']
    run(common + ['-shared', '-fPIC', root / 'tools/m8/test_boot_logo_fbdev.c', '-ldl', '-o', dest / 'fake.so'])
    if arch == 'mips':
        shutil.copyfile(a.payload / 'libm8-boot-logo.so', dest / 'gate.so')
    else:
        run(common + ['-shared', '-fPIC', '-DM8_BOOT_CORE_ONLY', board / 'm8-boot-logo.c',
                      board / 'm8-boot-logo-gate.c', '-ldl', '-pthread', '-o', dest / 'gate.so'])
    run(common + ['-shared', '-fPIC', '-I' + str(a.sdk / 'staging/usr/include/SDL'),
                  board / 'm8-display.c', board / 'm8-native-video.c', board / 'm8-damage.c',
                  '-ldl', '-o', dest / 'display.so'])
    run(common + [root / 'tools/m8/test_boot_logo_preload.c', dest / 'fake.so', '-ldl',
                  '-Wl,-rpath,' + str(dest), '-o', dest / 'probe'])
    run(common + ['-I' + str(a.sdk / 'staging/usr/include/SDL'),
                  root / 'tools/m8/test_boot_logo_native.c', dest / 'fake.so', '-ldl',
                  '-Wl,-rpath,' + str(dest), '-o', dest / 'native-probe'])
    run(common + [board / 'm8-boot-logo.c', '-o', dest / 'show'])
    for mode, age in [('native', 0), ('alias', 20), ('sdl', 20), ('update', 20),
                      ('rects2', 20), ('flip', 20), ('rect', 20), ('rects', 20), ('black', 20),
                      ('exec', 0), ('reject-fixed', 20), ('cancel', 0)]:
        if mode == 'exec' and arch == 'mips':
            # User-mode exec uses the host binfmt interpreter, not the guest
            # kernel. The actual MIPS-kernel harness covers this path instead.
            continue
        with tempfile.TemporaryDirectory(prefix='m8-boot-preload-') as temp:
            work = Path(temp)
            fb, state = work / 'fb', work / 'state'
            fb.write_bytes(b'\x55' * 256)
            state.write_text(f'M8_BOOT_LOGO_V1 {int((time.monotonic()-age)*1000)}\n')
            state.chmod(0o600)
            env = os.environ.copy()
            env.update(M8_TEST_FB=str(fb), M8_TEST_STATE=str(state),
                       LILSMOKE_M8_BOOT_STATE='/run/m8-boot-logo.state', LILSMOKE_HCFB='1')
            if mode == 'exec':
                env['LILSMOKE_M8_LOADING_GAME'] = '1'
            preload = ':'.join(str(dest / n) for n in ('gate.so', 'display.so', 'fake.so'))
            command = [dest / 'probe', 'native' if mode == 'reject-fixed' else mode]
            if mode == 'reject-fixed':
                env['M8_TEST_REJECT_FIXED'] = '1'
            if arch == 'mips':
                command = ['qemu-mipsel', '-cpu', '74Kf', '-L', a.sdk / 'staging', '-E', 'LD_PRELOAD=' + preload] + command
            else:
                env['LD_PRELOAD'] = preload
            before = time.monotonic()
            if mode == 'cancel':
                process = subprocess.Popen(list(map(str, command)), env=env)
                try:
                    time.sleep(0.5)
                    state.unlink()
                    process.send_signal(signal.SIGTERM)
                    assert process.wait(timeout=2) == 143
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
            elif mode == 'reject-fixed':
                failed = subprocess.run(list(map(str, command)), env=env, timeout=30)
                assert failed.returncode == 70 and not state.exists(), (failed.returncode, state.exists())
            else:
                run(command, env=env, timeout=30)
            elapsed = time.monotonic() - before
            if age == 0 and mode not in ('cancel', 'exec'):
                assert elapsed >= 5.98, elapsed
            elif arch == 'host':
                assert elapsed < 2, elapsed
            print(f'PASS {arch} {mode}: wall={elapsed:.3f}s, readiness_age={age}s', flush=True)
    for mode in ('rgb16', 'rgb32', 'menu16', 'menu32', 'flip'):
        with tempfile.TemporaryDirectory(prefix='m8-native-loading-') as temp:
            work = Path(temp)
            fb, state = work / 'fb', work / 'state'
            fb.write_bytes(b'\x55' * 256)
            state.write_text(f'M8_BOOT_LOGO_V1 {int(time.monotonic()*1000)}\n')
            state.chmod(0o600)
            env = dict(os.environ, M8_TEST_FB=str(fb), M8_TEST_STATE=str(state),
                       LILSMOKE_M8_BOOT_STATE='/run/m8-boot-logo.state',
                       LILSMOKE_M8_LOADING_GAME='1', LILSMOKE_HCFB='1',
                       LILSMOKE_M8_NATIVE_VIDEO='1')
            preload = ':'.join(str(dest / n) for n in ('gate.so', 'display.so', 'fake.so'))
            command = [dest / 'native-probe', mode]
            if arch == 'mips':
                command = ['qemu-mipsel', '-cpu', '74Kf', '-L', a.sdk / 'staging',
                           '-E', 'LD_PRELOAD=' + preload] + command
            else:
                env['LD_PRELOAD'] = preload
            run(command, env=env, timeout=30)

# Exercise the actual show helper with the packaged source at TV resolution.
# This is a rendering preview, not an edit of the supplied artwork.
with tempfile.TemporaryDirectory(prefix='m8-boot-show-') as temp:
    work = Path(temp)
    fb, state = work / 'fb', work / 'state'
    fb.write_bytes(bytes(1280 * 720 * 8))
    env = os.environ.copy()
    env.update(M8_TEST_FB=str(fb), M8_TEST_WIDTH='1280', M8_TEST_HEIGHT='720',
               M8_TEST_REQUIRE_MODE_AND_PAN='1', M8_TEST_NO_ALPHA='1',
               LD_PRELOAD=str(a.out / 'host/fake.so'))
    started = time.monotonic()
    run([a.out / 'host/show', '--show', a.payload / 'boot-logo.argb', state], env=env, timeout=15)
    assert time.monotonic() - started >= 6, 'logo framebuffer released before minimum hold'
    assert state.read_text().startswith('M8_BOOT_LOGO_V1 ')
    data = fb.read_bytes()[:1280*720*4]
    assert any(data[i] for i in range(0, len(data), 4)), 'blank logo'
    assert all(value == 255 for value in data[3::4]), 'transparent logo'
    from PIL import Image
    Image.frombytes('RGBA', (1280, 720), data, 'raw', 'BGRA').convert('RGB').save(a.out / 'logo-preview.png')
    print('PASS actual helper: packaged native-WebP raster, 1280x720 hcfb preview, successful-show timing token')

run(['qemu-mipsel', '-cpu', '74Kf', a.payload / 'm8-boot-logo', '--self-check'])
repeated = a.out / 'raster-repeated.argb'
run([a.payload.parent / 'pack-boot-logo', a.payload.parent / 'logo-source.webp', repeated])
assert hashlib.sha256(repeated.read_bytes()).digest() == hashlib.sha256((a.payload / 'boot-logo.argb').read_bytes()).digest()
print('PASS native-WebP raster packaging is byte-for-byte deterministic')

with tempfile.TemporaryDirectory(prefix='m8-live-loading-') as temporary:
    work = Path(temporary)
    fb, state = work / 'fb', work / 'state'
    fb.write_bytes(bytes(1280 * 720 * 8))
    (work / 'active').touch(mode=0o600)
    (work / 'progress').write_text('5\tPreparing runtime\n')
    env = dict(os.environ, M8_TEST_FB=str(fb), M8_TEST_WIDTH='1280',
               M8_TEST_HEIGHT='720', LD_PRELOAD=str(a.out / 'host/fake.so'))
    process = subprocess.Popen(list(map(str, [a.out / 'host/show', '--show',
        a.payload / 'boot-logo.argb', state, '--hold', work, '--owner', os.getpid()])), env=env)
    try:
        time.sleep(6.2)
        assert process.poll() is None and state.exists(), 'logo closed before UI readiness'
        before = fb.read_bytes()
        (work / 'progress').write_text('65\tPreparing runtime\n')
        time.sleep(.25)
        after = fb.read_bytes()
        assert before != after and any(after), 'progress did not update scanout'
        Image.frombytes('RGBA', (1280,720), after[:1280*720*4], 'raw','BGRA').convert('RGB').save(a.out / 'loading-65.png')
        (work / 'active').unlink()
        assert process.wait(timeout=2) == 0 and (work / 'stopped').exists()
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)
print('PASS live loading: retains logo beyond six seconds, blue percentage progress, bounded release acknowledgement')
