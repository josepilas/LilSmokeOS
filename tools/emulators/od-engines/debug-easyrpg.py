import os
from pathlib import Path
import socket
import subprocess
import time
import sys

R = Path('/root/lilsmoke-emulator-expansion-20261008')
S = R / 'engine-sysroot'
Q = R / 'engine-build/native-qa'
binary = R / 'engine-build/easyrpg/easyrpg-player'
shim=Q/'menu-probe.so'
if '--mame' in sys.argv:
    binary=R/'native-candidates/bin/mame4all'
    shim=R/'engine-build/legacy-menu-qa.so'
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           SDL_RENDER_DRIVER='software', OD_MENU_QA_FRAME=str(Q / 'easyrpg-debug.bmp'))
libs = ':'.join(map(str, [R/'od-console-qa-runtime/runtime/lib',R/'engine-build/sdl-qa',R/'built-native/lib',S/'usr/lib',S/'lib']))
with (Q/'easyrpg-debug.log').open('w') as log, (Q/'easyrpg-backtrace.txt').open('w') as trace:
    vm = subprocess.Popen(['qemu-mipsel', '-g', str(port), '-cpu', '74Kf', '-L', str(S),
        '-E', 'LD_LIBRARY_PATH='+libs, '-E', 'LD_PRELOAD='+str(shim), str(binary)],
        env=env, cwd=Q/'empty', stdout=log, stderr=log)
    try:
        time.sleep(1)
        subprocess.run(['gdb-multiarch', '-q', '-batch', str(binary),
            '-ex', 'set sysroot /', '-ex', 'set solib-search-path '+libs,
            '-ex', 'target remote 127.0.0.1:'+str(port), '-ex', 'continue',
            '-ex', 'thread apply all bt', '-ex', 'info registers', '-ex', 'detach'],
            stdout=trace, stderr=trace, timeout=40)
    finally:
        vm.terminate()
        try:
            vm.wait(timeout=3)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait()
print((Q/'easyrpg-backtrace.txt').read_text())
