#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build/run isolated host hotkey tests; never overwrite qualification artifacts."""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def run(command, **kwargs):
    print(shlex.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def test(out):
    out.mkdir(parents=True, exist_ok=True)
    ui = ROOT / 'package/lilsmoke-ui/src/src'
    src = ROOT / 'package/lilsmoke-scripts/src'
    runtime = ROOT / 'tools/treefrog-converter/runtime'
    flags = ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-I' + str(ui), '-I' + str(src),
             '-I' + str(ROOT / 'package/lilsmoke-usb-gamepads/src'), '-I' + str(runtime)]
    flags += shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True))
    libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2'], text=True))
    direct = out / 'direct-hotkey'
    independent = out / 'runtime-hotkey'
    run(['cc', *flags, ROOT / 'tools/treefrog-compat/treefrog-hotkey-test.c', '-o', direct, *libs])
    run(['cc', *flags, runtime / 'tests/test_hotkey.c', '-o', independent, *libs, '-ldl', '-lm'])
    run(['cc', *flags, '-D_POSIX_C_SOURCE=200809L', ROOT / 'tools/treefrog-compat/input_test.c', '-o', out / 'input-mapping'])
    run([out / 'input-mapping'], timeout=10)
    run([direct], timeout=10)
    run(['env', 'SDL_VIDEODRIVER=dummy', 'SDL_AUDIODRIVER=dummy', independent], timeout=10)
    build = out / 'runtime'
    run([sys.executable, runtime / 'build.py', '--target', 'host', '--out', build], timeout=60)
    run([sys.executable, runtime / 'tests/test_runtime.py', '--build', build], timeout=90)
    run([sys.executable, runtime / 'tests/test_hotkey_runtime.py', '--build', build], timeout=45)
    native = out / 'native-input-audit'
    run(['c++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
         '-I' + str(ui), '-I' + str(src),
         *shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True)),
         ROOT / 'tools/treefrog-compat/treefrog-hotkey-native-input-test.cpp',
         *[ui / name for name in ('Input.cpp', 'Keymap.cpp', 'Log.cpp')], '-o', native, *libs], timeout=90)
    run([native, src / 'treefrog-keymap.txt'], timeout=20)
    doom = out / 'chocolate-doom-profile'
    run(['c++', '-std=c++17', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
         '-I' + str(ui), '-I' + str(src),
         *shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True)),
         ROOT / 'tools/treefrog-compat/treefrog-hotkey-chocolate-doom-test.cpp',
         *[ui / name for name in ('Input.cpp', 'Keymap.cpp', 'Log.cpp')], '-o', doom, *libs], timeout=90)
    run([doom], timeout=20)
    print('HOTKEY_HOST_SUITE passed (API/timing only; no physical-device or mount qualification)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.out:
        if args.out.exists():
            parser.error('--out must be a new directory; frozen artifacts must not be overwritten')
        test(args.out.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='treefrog-hotkey-host-') as temporary:
            test(Path(temporary))
