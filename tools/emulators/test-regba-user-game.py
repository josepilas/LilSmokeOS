"""Private MIPS ReGBA test using user ROM/stock BIOS, never a release payload."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys
import time
import socket
from PIL import Image

P = Path(__file__).resolve().parents[2]
ROOT = Path('/root/lilsmoke-emulator-expansion-20261008')
B = Path('/root/lilsmoke-os/build/buildroot/output')
SYS = B / 'staging'
stock = Path('/path/to/local-input/gba_bios.bin')
rom = P / '../../../work/m8-added-roms/gba.zip'
assert stock.stat().st_size == 16384
original_sha = hashlib.sha256(stock.read_bytes()).hexdigest()
out = P / 'test-results/opendingux-pico-gba-2026-10-09/regba-game'
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='regba-private-', dir=ROOT) as name:
    work = Path(name)
    home = work / 'home'
    home.mkdir()
    env = dict(os.environ, HOME=str(home), BIOS_DIR=str(stock.parent),
               LILSMOKE_OD_ENGINE='regba', LILSMOKE_OD_SYSTEM='gba',
               LILSMOKE_ARCHIVE_CACHE=str(ROOT / 'core-qa/user-content'),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software',
               OD_QA_OUTPUT=str(work), OD_QA_BEFORE_AT='12', OD_QA_AFTER_AT='25',
               OD_QA_QUIT_AT='30', OD_QA_KEYS='14:13:0.2,18:13:0.2')
    core = ROOT / 'od-console-stage/storage/emulators/opendingux/regba'
    content = subprocess.check_output(['qemu-mipsel', '-cpu', '74Kf', '-L', str(SYS),
        str(ROOT / 'routing-qa/resolver-mips'), 'gba', str(rom)], env=env, text=True).strip()
    shim = work / 'observer.so'
    subprocess.run([str(B / 'host/bin/mipsel-buildroot-linux-musl-gcc'), '-shared', '-fPIC',
                    '-O2', '-I' + str(SYS / 'usr/include/SDL'),
                    str(P / 'tools/emulators/od-consoles/qa-sdl12.c'), '-lSDL', '-ldl',
                    '-o', str(shim)], check=True)
    libraries = ':'.join(map(str, [ROOT / 'od-console-qa-runtime/runtime/lib',
                                  SYS / 'usr/lib', SYS / 'lib']))
    command = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(SYS), '-E', 'LD_LIBRARY_PATH=' + libraries,
               '-E', 'LD_PRELOAD=' + str(shim), str(core), content]
    if '--debug' in sys.argv:
        with socket.socket() as connection:
            connection.bind(('127.0.0.1', 0)); port = connection.getsockname()[1]
        with (out / 'debug-run.log').open('w') as log:
            process = subprocess.Popen(command[:1] + ['-g', str(port)] + command[1:], env=env,
                                       cwd=ROOT / 'od-console-stage/storage/emulators/share/regba',
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                time.sleep(1)
                with (out / 'backtrace.txt').open('w') as trace:
                    subprocess.run(['gdb-multiarch', '-q', '-batch', str(core),
                                    '-ex', 'set sysroot ' + str(SYS),
                                    '-ex', 'target remote 127.0.0.1:' + str(port),
                                    '-ex', 'continue', '-ex', 'thread apply all bt',
                                    '-ex', 'info registers', '-ex', 'x/12i $pc-16'],
                                   stdout=trace, stderr=subprocess.STDOUT, timeout=40)
            finally:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
        print((out / 'backtrace.txt').read_text())
        raise SystemExit(0)
    with (out / 'run.log').open('w') as log:
        result = subprocess.run(command, env=env, cwd=ROOT / 'od-console-stage/storage/emulators/share/regba',
                                stdout=log, stderr=subprocess.STDOUT, timeout=45)
    if result.returncode != 0:
        (out / 'result.json').write_text(json.dumps(dict(passed=False, exit=result.returncode,
            binary_sha256=hashlib.sha256(core.read_bytes()).hexdigest(), bios_bundled=False,
            scope='private native MIPS game test; do not deploy this candidate'), indent=2) + '\n')
        raise RuntimeError('ReGBA game failed with exit ' + str(result.returncode) + '; see run.log/backtrace.txt')
    frames = {}
    for label in ('before', 'after'):
        frame = Image.open(work / (label + '.bmp')).convert('RGB')
        frame.save(out / (label + '.png'))
        assert len(frame.getcolors(frame.width * frame.height)) > (20 if label=='after' else 1), 'blank/non-game image'
        frames[label] = hashlib.sha256(frame.tobytes()).hexdigest()
    assert frames['before'] != frames['after'], 'no rendered game progression'
    report = dict(passed=True, engine='ReGBA', binary_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
                  input='two synthetic Start presses', frames=frames, user_bios_sha256=original_sha,
                  bios_bundled=False, hardware_audio_tested=False, hardware_performance_tested=False,
                  scope='native MIPS user-mode game/render/input/quit, headless SDL; not the physical M8')
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
assert hashlib.sha256(stock.read_bytes()).hexdigest() == original_sha
print('PASS native ReGBA user game renders, progresses and quits; stock BIOS unchanged and not bundled.')
