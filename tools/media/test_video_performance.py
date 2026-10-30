"""Serial baseline/optimized software-video measurements; no card writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

P = Path(__file__).resolve().parents[2]
SDK = Path('/root/lilsmoke-os/build/buildroot/output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stage', choices=['baseline', 'optimized'], required=True)
    parser.add_argument('--owner-source', type=Path)
    args = parser.parse_args()
    os.nice(10)
    os.sched_setaffinity(0, {1})
    folder = args.out / args.stage
    folder.mkdir(parents=True, exist_ok=True)
    source = P / 'package/lilsmoke-media/src'
    if args.stage == 'baseline':
        for name in ('Decoder.cpp', 'Decoder.h', 'NativeVideo.cpp', 'Player.cpp'):
            if (folder / name).exists():
                continue
            shutil.copy2(source / name, folder / name)
        source = folder

    def run(name, command, env=None, timeout=240):
        result = subprocess.run(list(map(str, command)), env=env, capture_output=True,
                                text=True, timeout=timeout)
        (folder / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError((name, result.returncode, result.stderr[-3000:]))
        return result.stdout

    fixtures = Path('/root/lilsmoke-media-fixtures')
    inputs = [fixtures / name for name in ('Clip.mp4', 'Clip.webm', 'Clip.mkv')]
    private = args.out / 'private'
    private.mkdir(exist_ok=True)
    for name in ('Queen - Radio Ga Ga Official Video.webm', 'ssstwitter.com_1782699502231.mp4'):
        original = (args.owner_source or Path('/mnt/e/media')) / name
        if original.exists():
            clip = private / ('owner' + original.suffix)
            if not clip.exists():
                run('trim' + original.suffix, ['ffmpeg', '-nostdin', '-v', 'error', '-i', original,
                                             '-t', '4', '-map', '0', '-c', 'copy', clip])
            inputs.append(clip)
    results = {}
    for target in ('host', 'mips'):
        env = os.environ.copy()
        compiler, pkg, execute = 'g++', 'pkg-config', []
        if target == 'mips':
            env.update(PKG_CONFIG_SYSROOT_DIR=str(SDK / 'staging'), PKG_CONFIG_PATH='',
                       PKG_CONFIG_LIBDIR=str(SDK / 'staging/usr/lib/pkgconfig'))
            compiler, pkg = SDK / 'host/bin/mipsel-buildroot-linux-musl-g++', SDK / 'host/bin/pkg-config'
            execute = ['qemu-mipsel', '-cpu', '74Kf', '-L', SDK / 'staging']
        config = shlex.split(subprocess.check_output([str(pkg), '--cflags', '--libs', 'sdl2', 'SDL2_image', 'SDL2_ttf',
                             'libavformat', 'libavcodec', 'libavutil', 'libswscale', 'libswresample'],
                             env=env, text=True))
        binary = folder / ('profile-' + target)
        flags = ['-DMEDIA_ARGB_PROFILE'] if args.stage == 'optimized' else []
        run('build-' + target, [compiler, '-O2', '-g0', '-std=c++17', *flags,
            '-I' + str(source), '-I' + str(P / 'package/lilsmoke-media/src'),
            '-I' + str(P / 'package/lilsmoke-ui/src/src'),
            P / 'tools/media/profile_video.cpp', source / 'Decoder.cpp',
            P / 'package/lilsmoke-ui/src/src/Renderer.cpp', P / 'package/lilsmoke-ui/src/src/Log.cpp',
            P / 'package/lilsmoke-ui/src/src/Font.cpp',
            *config, '-ldl', '-lwebp', '-o', binary], env)
        samples = {}
        for file in inputs:
            samples[file.name] = [json.loads(run(target + '-' + file.name + '-' + str(n),
                                      [*execute, binary, file], env)) for n in range(3)]
        results[target] = dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), samples=samples)
    results['inputs'] = {str(file): hashlib.sha256(file.read_bytes()).hexdigest() for file in inputs}
    (folder / 'report.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
