#!/usr/bin/env python3
"""Isolated host/MIPS layout tests and production-helper file-backed hcfb previews."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

from PIL import Image

P = Path(__file__).resolve().parents[2]
BOARD = P / 'board/segam-m8-v6-stock'
SIZES = ((320, 240), (640, 480), (856, 480), (1280, 720))
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
MIPS = ['-mips32r2', '-mtune=74kc', '-mdspr2', '-mfp32', '-mhard-float']
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--sdk', type=Path, required=True)
ap.add_argument('--payload', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--before', type=Path, required=True)
a = ap.parse_args()
a.out.mkdir(exist_ok=False)
cc = a.sdk / 'host/bin/mipsel-buildroot-linux-musl-gcc'
core = BOARD / 'm8-boot-logo.c'
report = dict(passed=False, physical_hcfb_tested=False, cases=[])
# RAM-control tokens require native Unix owner/mode semantics, not a DrvFS mount.
fixture_root = Path(tempfile.mkdtemp(prefix='layout-fixture-', dir=a.payload.parent))
report['fixture_root'] = str(fixture_root)


def run(command, log, **kwargs):
    with log.open('w') as output:
        subprocess.run(list(map(str, command)), check=True, stdout=output,
                       stderr=subprocess.STDOUT, timeout=180, **kwargs)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


old, new = a.before.read_text(), core.read_text()
start, end = 'int m8_boot_draw(', 'static void progress_box('
assert old.split(start)[0] == new.split(start)[0]
assert old.split(end)[1] == new.split(end)[1], 'non-raster production changes'
assert sha(BOARD / 'boot-logo.webp') == sha(a.payload.parent / 'logo-source.webp')
report['webp_sha256'] = sha(BOARD / 'boot-logo.webp')
raster_w, raster_h = struct.unpack('<II', (a.payload / 'boot-logo.argb').read_bytes()[8:16])
report['protected_sources'] = {name: sha(BOARD / name) for name in (
    'm8-boot-logo.h', 'm8-boot-logo-gate.c', 'm8-native-video.c', 'm8-launch.sh')}

for arch, compiler in (('host', 'cc'), ('mips', cc)):
    dest = a.out / arch
    dest.mkdir()
    fixture = fixture_root / arch
    fixture.mkdir()
    common = [compiler] + FLAGS + (MIPS if arch == 'mips' else [])
    prefix = ['qemu-mipsel', '-cpu', '74Kf', '-L', a.sdk / 'staging'] if arch == 'mips' else []
    unit = dest / 'bounds'
    run(common + ['-DM8_BOOT_CORE_ONLY', P / 'tools/m8/test_boot_logo_layout.c', core,
                  '-static' if arch == 'mips' else '-g', '-o', unit], dest / 'bounds-build.log')
    run(prefix + [unit], dest / 'bounds.log')
    if arch == 'host':
        sanitized = dest / 'bounds-sanitized'
        run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-DM8_BOOT_CORE_ONLY', '-fsanitize=address,undefined',
             P / 'tools/m8/test_boot_logo_layout.c', core, '-o', sanitized], dest / 'sanitized-build.log')
        run([sanitized], dest / 'sanitized.log')
    # Same production helper, dynamically linked solely to inject fake device syscalls.
    # The isolated payload remains the normal static build, without any fixture.
    run(common + ['-shared', '-fPIC', P / 'tools/m8/test_boot_logo_fbdev.c',
                  '-ldl', '-o', dest / 'fake.so'], dest / 'fixture-build.log')
    run(common + [core, '-o', dest / 'show'], dest / 'show-build.log')
    run(common + ['-I' + str(BOARD), a.before, '-o', dest / 'show-before'], dest / 'before-build.log')

    def command(name):
        preload = str(dest / 'fake.so')
        return prefix + (['-E', 'LD_PRELOAD=' + preload] if arch == 'mips' else []) + [dest / name]

    def environment(fb, w, h):
        env = dict(os.environ, M8_TEST_FB=str(fb), M8_TEST_WIDTH=str(w), M8_TEST_HEIGHT=str(h),
                   M8_TEST_REQUIRE_MODE_AND_PAN='1', M8_TEST_NO_ALPHA='1')
        if arch == 'host':
            env['LD_PRELOAD'] = str(dest / 'fake.so')
        return env

    for w, h in SIZES:
        work = fixture / f'{w}x{h}'
        work.mkdir()
        logs = dest / f'{w}x{h}'
        logs.mkdir()
        frames = {}
        for name in ('show', 'show-before'):
            fb = work / f'{name}.fb'
            fb.write_bytes(bytes(w * h * 8))
            env = environment(fb, w, h)
            active, progress = work / 'active', work / 'progress'
            active.touch(mode=0o600)
            progress.write_text('5\tPreparing runtime\n')
            state = work / f'{name}.state'
            log_path = logs / f'{name}.log'
            with log_path.open('w') as log:
                proc = subprocess.Popen(list(map(str, command(name) + ['--show',
                    a.payload / 'boot-logo.argb', state, '--hold', work, '--no-minimum'])),
                    env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    def await_frame(percent):
                        deadline = time.monotonic() + 15
                        y, x, width = h * 86 // 100, w // 5, w * 3 // 5
                        filled = width * percent // 100
                        previous = None
                        while time.monotonic() < deadline:
                            assert proc.poll() is None, log_path.read_text()
                            data = fb.read_bytes()
                            row = data[(y*w+x)*4:(y*w+x+width)*4]
                            if (state.exists() and row == b'\xed\x97\x26\xff' * filled
                                    + b'\x40\x38\x30\xff' * (width-filled)):
                                if data == previous:
                                    return data
                                previous = data
                            time.sleep(.025)
                        raise AssertionError(f'{arch} {w}x{h} progress {percent} timed out')

                    first = await_frame(5)
                    if name == 'show' and (w, h) == (1280, 720):
                        time.sleep(6.05)
                        assert proc.poll() is None and state.exists(), 'held helper released before readiness'
                        report[f'{arch}_held_beyond_six_seconds'] = True
                    progress.write_text('65\tPreparing runtime\n')
                    data = await_frame(65)
                    cut = w * (h * 86 // 100) * 4
                    assert data[:cut] == first[:cut], 'progress repainted artwork'
                    assert data[w*h*4:] == bytes(w*h*4), 'offscreen page modified'
                    frame = data[:w*h*4]
                    assert set(frame[3::4]) == {255}, 'nonopaque scanout'
                    frames[name] = frame
                    Image.frombytes('RGBA', (w, h), frame, 'raw', 'BGRA').convert('RGB').save(
                        a.out / f'{arch}-{w}x{h}{"-before" if name.endswith("before") else ""}.png')
                    active.unlink()
                    assert proc.wait(timeout=5) == 0
                    assert (work / 'stopped').exists() and state.read_text().startswith('M8_BOOT_LOGO_V1 ')
                    (work / 'stopped').unlink()
                finally:
                    if proc.poll() is None:
                        proc.terminate()
                        proc.wait(timeout=5)
        cut = w * (h * 86 // 100) * 4
        assert frames['show'][cut:] == frames['show-before'][cut:], 'progress appearance changed'
        assert frames['show'][:cut] != frames['show-before'][:cut], 'artwork did not move'
        area, fit_w = h * 4 // 5, w * 9 // 10
        fit_h = area * 9 // 10
        want_w = fit_h * raster_w * 720 * w // (raster_h * 1280 * h)
        if want_w <= fit_w:
            fit_w = want_w
        else:
            fit_h = fit_w * raster_h * 1280 * h // (raster_w * 720 * w)
        case = dict(arch=arch, width=w, height=h, screen_width=1280, screen_height=720,
                    sha256=hashlib.sha256(frames['show']).hexdigest(), progress_unchanged=True,
                    artwork_bounds=[(w-fit_w)//2, (area-fit_h)//2, fit_w, fit_h],
                    bar_y=h*86//100)
        report['cases'].append(case)
        print(f'PASS {arch} {w}x{h}: actual helper, 5->65 progress, unchanged bar, opaque scanout', flush=True)

    # Keep each real helper's framebuffer ownership for at least six seconds.
    cold = fixture / 'cold'
    cold.mkdir()
    fb = cold / 'fb'
    fb.write_bytes(bytes(320*240*8))
    began = time.monotonic()
    run(command('show') + ['--show', a.payload / 'boot-logo.argb', cold / 'state'],
        dest / 'cold-show.log', env=environment(fb, 320, 240))
    elapsed = time.monotonic() - began
    assert elapsed >= 6, elapsed
    report[f'{arch}_cold_hold_seconds'] = elapsed

for w, h in SIZES:
    host = next(c for c in report['cases'] if c['arch']=='host' and c['width']==w and c['height']==h)
    mips = next(c for c in report['cases'] if c['arch']=='mips' and c['width']==w and c['height']==h)
    assert host['sha256'] == mips['sha256'], 'host/MIPS scanout differs'
run(['qemu-mipsel', '-cpu', '74Kf', a.payload / 'm8-boot-logo', '--self-check'], a.out / 'payload-self-check.log')
run([a.payload.parent / 'pack-boot-logo', BOARD / 'boot-logo.webp', a.out / 'repacked.argb'], a.out / 'repack.log')
assert sha(a.out / 'repacked.argb') == sha(a.payload / 'boot-logo.argb')
previous_raster = P / 'build/retroarch-recovery-20261021/runtime/boot-logo.argb'
if previous_raster.is_file():
    assert sha(previous_raster) == sha(a.payload / 'boot-logo.argb')
    report['candidate38_raster_unchanged'] = True
shutil.copyfile(a.out / 'mips-1280x720.png', a.out.parent / 'preview-1280x720.png')
shutil.copyfile(a.out / 'mips-856x480.png', a.out.parent / 'preview-856x480.png')
report.update(passed=True, host_mips_pixel_identical=True, non_raster_source_unchanged=True,
              deterministic_raster=True, held_progress_updates=True, offscreen_page_unchanged=True,
              fixture_only_dynamic_helpers=True, static_payload_self_check=True)
(a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS layout, sanitizer, static MIPS bounds/payload, cold holds, host/MIPS identical previews')
