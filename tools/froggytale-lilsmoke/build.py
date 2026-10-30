#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Serial, CPU-1 cross build. All outputs remain in this tool's directory."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SDK = Path(os.environ.get('SDK', '/root/lilsmoke-os/build/buildroot/output'))
OUT = HERE / '_build/mips'


def run(cmd, log):
    prefix = ['taskset', '-c', os.environ.get('FROGGY_CPU', '1'), 'nice', '-n', '15',
              'prlimit', '--as=805306368', '--']
    with log.open('a') as f:
        f.write(json.dumps(list(map(str, cmd))) + '\n')
        f.flush()
        subprocess.run(prefix + list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, check=True)


def main():
    subprocess.run(['python3', '-B', str(HERE / 'fetch.py')], check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    up = HERE / '_build/upstream'
    stage = OUT / 'stage'
    # Pristine fetched files stay untouched; all target changes have a staged diff.
    shutil.copytree(up, stage, dirs_exist_ok=True)
    path = stage / 'src/loop.c'
    s = path.read_text()
    old = '        options.parseTxtr = true;'
    assert s.count(old) == 1
    s = s.replace(old, '        options.parseTxtr = false; /* LilSmoke: prepacked BYO atlas, no page decode. */')
    s = s.replace('        if (!args.headless)\n            options.parseAudo = true;',
        '        if (!args.headless || getenv("FROGGY_HEADLESS_AUDIO"))\n            options.parseAudo = true;')
    s = s.replace('        if (args.headless) {\n            audioSystem =',
        '        if (args.headless && !getenv("FROGGY_HEADLESS_AUDIO")) {\n            audioSystem =')
    s = '#include "lilsmoke_audio.h"\n' + s
    cleanup='        runner->audioSystem->vtable->destroy(runner->audioSystem);'
    assert s.count(cleanup)==1
    s=s.replace(cleanup,'        if (!args.headless || getenv("FROGGY_HEADLESS_AUDIO"))\n'
        '            logInfo("UNDERTALE_AUDIO ready=%d frames=%llu nonzero_frames=%llu\\n", LilSmokeAudio_ready(runner->audioSystem), '
        '(unsigned long long)LilSmokeAudio_frames(runner->audioSystem), '
        '(unsigned long long)LilSmokeAudio_nonzeroFrames(runner->audioSystem));\n'+cleanup)
    path.write_text(s)
    audio=OUT/'audio'
    subprocess.run(['python3','-B',str(HERE/'audio/stage.py'),'--upstream',str(stage),'--out',str(audio)],check=True)
    native = OUT / 'native'
    native.mkdir(exist_ok=True)
    # MIT transport only: no MPL-covered UI or adapter source linked into AGPL.
    shutil.copyfile(PROJECT / 'package/lilsmoke-ui/src/src/gamepad-state.h', native / 'gamepad-state.h')
    shutil.copyfile(PROJECT/'package/portsmoke-sm64/src/vendor/tinygl/src/font8x8_basic.h',native/'font8x8_basic.h')
    cc = SDK / 'host/bin/mipsel-buildroot-linux-musl-gcc'
    common = ['-std=gnu99', '-O2', '-fno-strict-aliasing', '-fwrapv', '-mips32r2',
              '-mabi=32', '-mfp32', '-mhard-float', '-ffunction-sections', '-fdata-sections',
              '-DENABLE_WAD16', '-DDISABLE_WAD14', '-DDISABLE_WAD17', '-DENABLE_SW_RENDERER',
              '-DBUTTERSCOTCH_VIDEO_NULL=1', '-DDISABLE_VM_GML_PROFILER', '-DDISABLE_VM_OPCODE_PROFILER',
              '-DDISABLE_VM_STUB_LOGS', '-DDISABLE_VM_TRACING', '-DDISABLE_SPATIAL_GRID_LOGS',
              '-DDISABLE_VM_EXCEPTIONS_LOGS', '-DBUTTERSCOTCH_COMMIT_HASH="5699418-lilsmoke"',
              '-DBUTTERSCOTCH_COMMIT_DATE="2026-10-27"', '-D_POSIX_C_SOURCE=200809L', '-D_DEFAULT_SOURCE']
    includes = [stage / p for p in ('src', 'src/image', 'src/video', 'src/audio', 'src/debug_font',
                'src/sw_renderer', 'vendor/stb/ds', 'vendor/stb/image', 'vendor/stb/vorbis',
                'vendor/bzip2', 'vendor/md5', 'vendor/miniz', 'vendor/sha1', 'vendor/base64')]
    includes += [SDK / 'staging/usr/include/SDL2', HERE / 'src', native]
    flags = common + [f'-I{p}' for p in includes]
    sources = sorted((stage / 'src').glob('*.c'))
    for directory in ('src/image', 'src/debug_font', 'src/sw_renderer', 'vendor/bzip2', 'vendor/miniz'):
        sources += sorted((stage / directory).glob('*.c'))
    sources += [stage / p for p in ('src/physics/physics.c', 'src/physics/disabled/physics_disabled.c',
        'src/video/null_video.c', 'src/cli/args.c', 'src/cli/stb_impl.c', 'vendor/md5/md5.c',
        'vendor/sha1/sha1.c', 'vendor/base64/base64.c')]
    sources += sorted((HERE / 'src').glob('*.c'))
    sources += [audio/'ma_audio_system.c']
    objects = []
    manifest = []
    start = time.monotonic()
    log = OUT / 'build.log'
    log.write_text('Serial GCC MIPS32r2/o32 hard-float musl; CPU 1 nice 15, 768 MiB AS\n')
    for index, source in enumerate(sources):
        name = source.relative_to(stage).as_posix() if source.is_relative_to(stage) else ('audio/' if source.is_relative_to(audio) else 'adapter/') + source.name
        obj = OUT / 'obj' / (name + '.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        local_flags=flags
        audio_headers=b''
        if name in ('src/loop.c','audio/ma_audio_system.c'):
            local_flags=flags+['-DUSE_MINIAUDIO','-DFROGGY_SDL_AUDIO=1',f'-I{audio}',f'-I{HERE / "audio"}',
                f'-I{stage / "src/audio/miniaudio"}',f'-I{stage / "vendor/miniaudio"}']
            audio_headers=(audio/'ma_audio_system.h').read_bytes()+(HERE/'audio/sdl_pull.inc').read_bytes()+(HERE/'audio/lilsmoke_audio.h').read_bytes()
        if name=='audio/ma_audio_system.c':
            local_flags=local_flags+['-fno-tree-vrp']
        command = [cc, *local_flags, '-c', source, '-o', obj]
        identity = hashlib.sha256(source.read_bytes() + audio_headers + json.dumps(list(map(str, local_flags))).encode() +
                                  b''.join(p.read_bytes() for p in (HERE / 'src').glob('*.h'))).hexdigest()
        stamp = obj.with_suffix('.key')
        if not obj.exists() or not stamp.exists() or stamp.read_text() != identity:
            print(f'[{index+1}/{len(sources)}] {name}', flush=True)
            run(command, log)
            stamp.write_text(identity)
        objects.append(obj)
        manifest.append(dict(path=name, sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    binary = OUT / 'froggytale-lilsmoke'
    temporary=binary.with_suffix('.new')
    run([cc, *flags, '-Wl,--gc-sections', *objects, '-lSDL2', '-lm', '-ldl', '-lpthread', '-latomic', '-o', temporary], log)
    temporary.replace(binary)
    with (OUT / 'elf.txt').open('w') as f:
        subprocess.run([str(SDK / 'host/bin/mipsel-buildroot-linux-musl-readelf'), '-h', '-A', '-l', '-d', str(binary)], stdout=f, check=True)
    (OUT / 'build.json').write_text(json.dumps(dict(sdk=str(SDK), cpu=os.environ.get('FROGGY_CPU', '1'),
        elapsed_seconds=time.monotonic()-start, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        flags=flags, sources=manifest), indent=2) + '\n')
    print(binary, flush=True)


if __name__ == '__main__':
    main()
