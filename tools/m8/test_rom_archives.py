"""Exercise shared extraction with synthetic ROM bytes, not game assets."""
import argparse
import binascii
import bz2
import gzip
import io
import json
import lzma
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--resolver', type=Path, required=True)
p.add_argument('--mips', action='store_true')
p.add_argument('--sysroot', type=Path)
p.add_argument('--rar-fixture', type=Path, help='libarchive test_read_format_rar5_stored.rar.uu')
a = p.parse_args()
if a.mips and not a.sysroot:
    p.error('--mips requires --sysroot')
if a.rar_fixture and not a.rar_fixture.is_file():
    p.error('--rar-fixture must be a regular uuencoded stored-RAR fixture')
command = ['qemu-mipsel', '-cpu', '74Kf', '-L', str(a.sysroot), str(a.resolver)] if a.mips else [str(a.resolver)]


def decode_uu_fixture(path):
    assert path.stat().st_size <= 1024 * 1024, 'RAR fixture exceeds test budget'
    output = bytearray()
    started = ended = False
    for line in path.read_bytes().splitlines():
        if not started:
            if line.startswith(b'begin '):
                started = True
            continue
        if line == b'end':
            ended = True
            break
        output.extend(binascii.a2b_uu(line))
    assert started and ended and output.startswith(b'Rar!\x1a\x07'), 'invalid stored-RAR uu fixture'
    return bytes(output)


def launcher_tests(root, env):
    source = Path(__file__).resolve().parents[2] / 'package/lilsmoke-scripts/src'
    scripts = [source / 'lilsmoke-launch', source / 'lilsmoke-retroarch']
    for script in scripts:
        text = script.read_text()
        for dead in ('extract_7z_rom', 'find_7z_tool', 'find_extracted_rom', 'archive_signature', 'rm -rf'):
            assert dead not in text, (script, dead)
        assert 'LILSMOKE_ROM_RESOLVER' in text
        subprocess.run(['sh', '-n', str(script)], check=True)
        if a.mips:
            busybox = a.sysroot.parent / 'target/bin/busybox'
            assert busybox.is_file(), busybox
            subprocess.run(command[:-1] + [str(busybox), 'sh', '-n', str(script)], check=True, timeout=30)

    # Exercise the real POSIX script with a fake RA process and private paths.
    # No emulator, /persist directory, app install or physical card is touched.
    bindir = root / 'test-bin'
    bindir.mkdir()
    capture = root / 'retroarch-capture.json'
    mock = bindir / 'retroarch'
    mock.write_text('#!/usr/bin/python3\nimport json,os,sys\n'
                    'with open(os.environ["TEST_CAPTURE"],"w") as out:\n'
                    ' json.dump({"args":sys.argv[1:],"volume":os.getenv("LILSMOKE_M8_VOLUME")},out)\n')
    mock.chmod(0o700)
    cores = root / 'cores'
    cores.mkdir()
    (cores / 'mgba_libretro.so').write_bytes(b'fake core for launcher argument test')
    config = root / 'settings.conf'
    defaults = root / 'defaults.conf'
    defaults.write_text('MASTER_VOLUME=50\n')
    config_reader = root / 'config-read'
    config_reader.write_text((source / 'lilsmoke-config-read').read_text())
    config_reader.chmod(0o700)
    base = root / 'base.cfg'
    base.write_text('audio_driver = "wrong"\ninput_joypad_driver = "wrong"\nvideo_aspect_ratio = 9\n')
    launcher_env = dict(env, PATH=str(bindir) + ':/usr/bin:/bin', TEST_CAPTURE=str(capture),
                        LILSMOKE_CORES_DIR=str(cores), LILSMOKE_CONFIG=str(config),
                        LILSMOKE_CONFIG_READER=str(config_reader),
                        LILSMOKE_DEFAULT_CONFIG=str(defaults),
                        LILSMOKE_RETROARCH_BASE_CONFIG=str(base),
                        LILSMOKE_RETROARCH_USER_CONFIG=str(root / 'user.cfg'),
                        LILSMOKE_RETROARCH_CORE_OPTIONS=str(root / 'options.cfg'),
                        LILSMOKE_RETROARCH_AUTOCONFIG=str(root / 'autoconfig'),
                        LILSMOKE_RETROARCH_PROFILES=str(root / 'profiles'),
                        LILSMOKE_RETROARCH_RUN_DIR=str(root / 'run'),
                        LILSMOKE_LOG_DIR=str(root / 'logs'), LILSMOKE_SAVE_ROOT=str(root / 'saves'),
                        LILSMOKE_STATE_ROOT=str(root / 'states'), LILSMOKE_SCREENSHOT_DIR=str(root / 'screenshots'),
                        LILSMOKE_PLAYLIST_DIR=str(root / 'playlists'), LILSMOKE_THUMBNAIL_DIR=str(root / 'thumbnails'),
                        LILSMOKE_M8_AUDIO='1', LILSMOKE_HCFB='1', LILSMOKE_ROM_RESOLVER=str(root / 'resolver-shim'))
    resolver = root / 'resolver-shim'
    # Route the actual shared resolver through QEMU for the MIPS test run.
    import shlex
    resolver.write_text('#!/bin/sh\nexec ' + ' '.join(shlex.quote(part) for part in command) + ' "$@"\n')
    resolver.chmod(0o700)
    game = root / 'Game.gba'
    for setting, expected in [('0', '0'), ('1', '0'), ('50', '25'), ('80', '40'),
                              ('080', '40'), ('99', '49'), ('100', '50'), ('999', '50'),
                              ('1000', '25'), ('-1', '25'), ('invalid', '25'),
                              ('"60" # Settings comment', '30')]:
        config.write_text('MASTER_VOLUME=' + setting + '\n')
        subprocess.run(['sh', str(scripts[1]), 'run', 'gba', str(game)],
                       env=launcher_env, check=True, timeout=30)
        data = json.loads(capture.read_text())
        assert data['volume'] == expected, (setting, data)
        assert data['args'][-1] == str(game)
        assert data['args'][data['args'].index('-L') + 1] == str(cores / 'mgba_libretro.so')
        runtime = (root / 'run/retroarch.cfg').read_text()
        assert 'audio_driver = "lilsmoke"' in runtime and 'audio_driver = "wrong"' not in runtime
        assert 'input_joypad_driver = "lilsmoke"' in runtime
        assert 'video_aspect_ratio = 1.3333' in runtime
        assert 'input_player1_a_btn = "8"' in runtime
        assert 'input_player1_x_btn = "9"' in runtime
        assert 'input_menu_toggle_btn = "16"' in runtime
        assert 'input_exit_emulator_btn = "17"' in runtime
        assert 'quit_press_twice = false' in runtime
        assert 'vrr_runloop_enable = true' in runtime
        assert 'video_windowed_fullscreen = false' in runtime
        assert 'rgui_internal_upscale_level = 0' in runtime
        assert 'menu_rgui_transparency = false' in runtime
        assert 'audio_enable = true' in runtime
        assert 'audio_enable_menu = false' in runtime
    launcher_env['LILSMOKE_M8_AUDIO'] = '0'
    launcher_env['LILSMOKE_M8_VOLUME'] = '47'
    subprocess.run(['sh', str(scripts[1]), 'menu'], env=launcher_env, check=True, timeout=30)
    assert json.loads(capture.read_text())['volume'] == '47'
    menu = json.loads(capture.read_text())
    assert '--verbose' in menu['args'] and '-L' not in menu['args']
    runtime = (root / 'run/retroarch.cfg').read_text()
    assert 'audio_enable = true' in runtime and 'audio_enable = false' not in runtime
    assert 'audio_enable_menu = false' in runtime
    extra = root / 'extra-cores'
    extra.mkdir()
    (extra / 'prboom_libretro.so').write_bytes(b'argument-only fixture')
    (extra / 'mgba_libretro.so').write_bytes(b'must not replace OS default')
    launcher_env['LILSMOKE_EXTRA_CORES_DIR'] = str(extra)
    subprocess.run(['sh', str(scripts[1]), 'menu'], env=launcher_env, check=True, timeout=30)
    merged = root / 'run/cores'
    assert (merged / 'prboom_libretro.so').resolve() == extra / 'prboom_libretro.so'
    assert (merged / 'mgba_libretro.so').resolve() == cores / 'mgba_libretro.so'
    assert f'libretro_directory = "{merged}"' in (root / 'run/retroarch.cfg').read_text()
    for selected in (str(extra / 'prboom_libretro.so'),):
        subprocess.run(['sh', str(scripts[1]), 'run', 'gba', str(game), selected],
                       env=launcher_env, check=True, timeout=30)
        args = json.loads(capture.read_text())['args']
        assert args[args.index('-L') + 1] == selected
    for invalid in ('../mgba_libretro.so', str(extra / '../cores/mgba_libretro.so'),
                    str(root / 'untrusted_libretro.so')):
        result = subprocess.run(['sh', str(scripts[1]), 'run', 'gba', str(game), invalid],
                                env=launcher_env, capture_output=True, timeout=30)
        assert result.returncode != 0, invalid
    print('PASS: optional SD cores discovered; OS duplicate precedence; validated full core paths; traversal refused.')
    print('PASS: launcher cleanup/syntax; Settings master volume, decimal/clamp/default handling; native mappings/config preserved.')


with tempfile.TemporaryDirectory(prefix='lilsmoke-rom-test-') as name:
    root = Path(name)
    cache = root / 'cache'
    env = dict(os.environ, LILSMOKE_ARCHIVE_CACHE=str(cache))
    payload = b'LilSmoke synthetic ROM\x00' * 100

    def resolve(system, archive, good=True):
        result = subprocess.run(command + [system, str(archive)], env=env, capture_output=True, text=True, timeout=30)
        if not good:
            assert result.returncode != 0, (archive, result)
            return
        assert result.returncode == 0, (archive, result.stderr)
        path = Path(result.stdout.strip())
        assert path.is_file() and path.read_bytes() == payload, (archive, path)
        return path

    for system, extension in [('nes', 'nes'), ('snes', 'sfc'), ('gb', 'gb'), ('gbc', 'gbc'),
                              ('gba', 'gba'), ('md', 'bin'), ('sms', 'sms'), ('gg', 'gg'),
                              ('pce', 'pce'), ('lynx', 'lnx'), ('n64', 'z64'), ('psx', 'bin')]:
        archive = root / (system + '.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('README.txt', 'not a ROM')
            z.writestr('nested/Game.' + extension.upper(), payload)
        first = resolve(system, archive)
        assert resolve(system, archive) == first

    for system, marker in [('atari2600', 'Game.a26'), ('atari7800', 'Game.a78'),
                           ('atari800', 'Game.atr'), ('ngpc', 'Game.ngc'),
                           ('wswan', 'Game.wsc'), ('pokemini', 'Game.min'),
                           ('doom', 'Game.wad'), ('cavestory', 'Doukutsu.exe'),
                           ('easyrpg', 'RPG_RT.ldb'), ('scummvm', 'Game.scummvm')]:
        archive = root / (system + '-data.zip')
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('nested/' + marker, payload)
            z.writestr('nested/Data/aux.bin', b'keep companion assets')
            if system == 'cavestory': z.writestr('unrelated.exe', b'never choose this')
            if system == 'easyrpg': z.writestr('unrelated.ldb', b'never choose this')
            if system == 'doom':
                z.writestr('prboom.wad', b'engine auxiliary resource, not a game')
                z.writestr('prboom-plus.wad', b'engine auxiliary resource, not a game')
        selected = resolve(system, archive)
        assert selected.name == marker, (system, selected)
        assert (selected.parent / 'Data/aux.bin').read_bytes() == b'keep companion assets'
    print('PASS: expansion markers, aliases and multi-file game assets retained.')

    original = root / 'Game.gba'
    original.write_bytes(payload)
    assert resolve('gba', original) == original
    for suffix, compress in [('gz', gzip.compress), ('bz2', bz2.compress), ('xz', lzma.compress)]:
        archive = root / ('Game.gba.' + suffix)
        archive.write_bytes(compress(payload))
        resolve('gba', archive)
    for suffix, mode in [('tar', 'w'), ('tar.gz', 'w:gz'), ('tgz', 'w:gz'),
                         ('tar.bz2', 'w:bz2'), ('tbz2', 'w:bz2'), ('tar.xz', 'w:xz'), ('txz', 'w:xz')]:
        archive = root / ('Game.' + suffix)
        with tarfile.open(archive, mode) as t:
            entry = tarfile.TarInfo('nested/Game.gba'); entry.size = len(payload)
            t.addfile(entry, io.BytesIO(payload))
        resolve('gba', archive)
    archive = root / 'Game.7z'
    subprocess.run(['7z', 'a', str(archive), original.name], cwd=root, check=True, capture_output=True)
    resolve('gba', archive)
    for member in ['../escape.gba', '/escape.gba', 'C:/escape.gba']:
        archive = root / 'unsafe.zip'
        with zipfile.ZipFile(archive, 'w') as z: z.writestr(member, payload)
        resolve('gba', archive, False)
    # libarchive normalizes Windows ZIP separators before exposing a safe
    # relative pathname. Accept that in-root result, never a traversal.
    with zipfile.ZipFile(archive, 'w') as z: z.writestr('dir\\Game.gba', payload)
    resolve('gba', archive)
    archive = root / 'links.tar'
    with tarfile.open(archive, 'w') as t:
        entry = tarfile.TarInfo('bad.gba'); entry.type = tarfile.SYMTYPE; entry.linkname = '../escape.gba'
        t.addfile(entry)
    resolve('gba', archive, False)
    archive = root / 'unknown.zip'
    with zipfile.ZipFile(archive, 'w') as z: z.writestr('readme.txt', payload)
    resolve('gba', archive, False)
    archive = root / 'romset.zip'
    with zipfile.ZipFile(archive, 'w') as z: z.writestr('part.bin', payload)
    result = subprocess.run(command + ['arcade', str(archive)], env=env, capture_output=True, text=True, check=True)
    assert Path(result.stdout.strip()) == archive
    arcade_7z = root / 'romset.7z'
    part = root / 'part.bin'
    part.write_bytes(payload)
    subprocess.run(['7z', 'a', str(arcade_7z), part.name], cwd=root, check=True, capture_output=True)
    result = subprocess.run(command + ['arcade', str(arcade_7z)], env=env,
                            capture_output=True, text=True, check=True, timeout=30)
    normalized = Path(result.stdout.strip())
    assert normalized.suffix == '.zip' and normalized != arcade_7z
    with zipfile.ZipFile(normalized) as packed:
        assert packed.namelist() == ['part.bin']
        assert packed.read('part.bin') == payload and packed.testzip() is None
    if a.rar_fixture:
        archive = root / 'stored.RAR'
        archive.write_bytes(decode_uu_fixture(a.rar_fixture))
        result = subprocess.run(command + ['arcade', str(archive)], env=env, capture_output=True,
                                text=True, check=True, timeout=30)
        normalized = Path(result.stdout.strip())
        assert normalized != archive and normalized.suffix == '.zip'
        assert normalized.resolve().is_relative_to(cache.resolve())
        with zipfile.ZipFile(normalized) as packed:
            assert packed.namelist() == ['helloworld.txt'], packed.namelist()
            assert packed.read('helloworld.txt') == b'hello libarchive test suite!\n'
            assert packed.testzip() is None
        cached = subprocess.run(command + ['fbneo', str(archive)], env=env, capture_output=True,
                                text=True, check=True, timeout=30)
        assert Path(cached.stdout.strip()) == normalized
        print('PASS: libarchive stored-RAR uu fixture -> arcade ZIP; exact member/content/CRC and alias-cache reuse.')
    else:
        print('SKIP: RAR fixture coverage (pass --rar-fixture for the libarchive stored-RAR fixture).')
    assert not (root / 'escape.gba').exists()
    launcher_tests(root, env)
print('PASS: every system ZIP/cache/native file; 7z, tar, gzip/bzip2/xz; traversal/link refusal; missing ROM refusal; arcade set passthrough.')
