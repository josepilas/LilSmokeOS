"""Exercise the actual POSIX native router with argument-capturing fixtures."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

P = Path(__file__).resolve().parents[2]
router = P / 'package/lilsmoke-scripts/src/lilsmoke-od-launch'
with tempfile.TemporaryDirectory(prefix='lilsmoke-native-launch-') as name:
    root = Path(name)
    binary = root / 'bin'
    binary.mkdir()
    capture = root / 'capture.json'
    mock = ('#!/usr/bin/python3\nimport json,os,sys\n'
            'with open(os.environ["TEST_CAPTURE"],"w") as out:\n'
            ' json.dump({"args":sys.argv[1:],"cwd":os.getcwd(),'
            '"env":dict(os.environ)},out)\n')
    systems = dict(pcsx4all='psx', picodrive='md', regba='gba', fceu320='nes', gnuboy='gb',
                   temper='pce', handy='lynx', stella='atari2600',
                   prosystem='atari7800', atari800='atari800', race='ngpc',
                   oswan='wswan', pokemini='pokemini', fba='arcade',
                   prboom='doom', nxengine='cavestory', easyrpg='easyrpg',
                   scummvm='scummvm')
    for engine in systems:
        path = binary / engine
        path.write_text(mock)
        path.chmod(0o700)
    resolver = root / 'resolver'
    resolver.write_text('#!/bin/sh\nprintf "%s\\n" "$3" >> "$TEST_RESOLVER_CALLS"\n'
                        'printf "%s\\n" "$TEST_CONTENT"\n')
    resolver.chmod(0o700)
    config = root / 'settings.conf'
    config.write_text('MASTER_VOLUME="080" # decimal\n')
    env = dict(os.environ, PATH='/usr/bin:/bin', TEST_CAPTURE=str(capture),
               TEST_RESOLVER_CALLS=str(root / 'resolver-calls'),
               LILSMOKE_ROM_RESOLVER=str(resolver),
               LILSMOKE_CONFIG_READER=str(P / 'package/lilsmoke-scripts/src/lilsmoke-config-read'),
               LILSMOKE_CONFIG=str(config), LILSMOKE_EXTRA_OD_DIR=str(binary),
               LILSMOKE_PCSX_BINARY=str(binary/'pcsx4all'),
               LILSMOKE_OD_HOME_ROOT=str(root / 'home'), LILSMOKE_ROM_ROOT=str(root / 'roms'),
               LILSMOKE_OD_SHARE_ROOT=str(root / 'share'), LILSMOKE_LOG_DIR=str(root / 'logs'),
               LILSMOKE_OD_ENGINE_SHARE_ROOT=str(root / 'engine-data'),
               LILSMOKE_HCFB='0', LILSMOKE_M8_AUDIO='1', DISPLAY='must be cleared',
               WAYLAND_DISPLAY='must be cleared', LD_PRELOAD='', LD_AUDIT='')
    content = root / 'Game data' / 'game.bin'
    content.parent.mkdir()
    content.write_bytes(b'argument-only test, not a ROM')
    env['TEST_CONTENT'] = str(content)
    for engine, system in systems.items():
        subprocess.run(['sh', str(router), engine], env=env, check=True, timeout=10)
        menu = json.loads(capture.read_text())
        assert not (root / 'resolver-calls').exists(), 'no-content menu invoked resolver'
        assert menu['env']['HOME'] == str(root / 'home' / engine)
        assert menu['env']['LILSMOKE_OD_SYSTEM'] == system
        assert menu['env']['LILSMOKE_OD_ROM_DIR'] == str(root / 'roms' / system)
        assert menu['env']['SDL_VIDEODRIVER'] == 'directfb'
        assert menu['env']['SDL_RENDER_DRIVER'] == 'software'
        assert 'no-cursor' in menu['env']['DFBARGS']
        assert menu['env']['LILSMOKE_POINTER_ENABLED'] == ('1' if engine=='scummvm' else '0')
        assert menu['env'].get('LILSMOKE_OD_INPUT_EXTERNAL') == ('1' if engine=='pcsx4all' else None)
        assert menu['env']['LILSMOKE_M8_VOLUME'] == '40'
        assert 'DISPLAY' not in menu['env'] and 'WAYLAND_DISPLAY' not in menu['env']
        if engine != 'scummvm': assert menu['args'] == [], (engine, menu)
        if engine == 'scummvm':
            content.write_text('scumm:monkey\n')
        subprocess.run(['sh', str(router), engine, system, str(content)],
                       env=env, check=True, timeout=10)
        game = json.loads(capture.read_text())
        args = game['args']
        if engine == 'nxengine': assert args == ['--data', str(content.parent)]
        elif engine == 'prboom': assert args == ['-iwad', str(content)]
        elif engine == 'easyrpg':
            project_id = hashlib.sha256(str(content.parent).encode()).hexdigest()[:24]
            assert args == ['--project-path', str(content.parent), '--save-path', str(root / 'home/easyrpg/saves' / project_id)]
        elif engine == 'scummvm': assert args[-2:] == ['--path=' + str(content.parent), 'scumm:monkey']
        else: assert args == [str(content)], (engine, args)
        (root / 'resolver-calls').unlink()
    for invalid in (['unknown'], ['picodrive', 'gba', str(content)], ['prboom', 'extra']):
        assert subprocess.run(['sh', str(router), *invalid], env=env,
                              capture_output=True, timeout=10).returncode != 0
    content.write_text('scumm:monkey; touch /bad\n')
    assert subprocess.run(['sh', str(router), 'scummvm', 'scummvm', str(content)],
                          env=env, capture_output=True, timeout=10).returncode != 0
    env['LILSMOKE_HCFB'] = '1'
    assert subprocess.run(['sh', str(router), 'prboom'], env=env,
                          capture_output=True, timeout=10).returncode != 0
print('PASS: 18 native menu/game argv contracts; PCSX shared native path, single evdev owner, no cursor; per-engine persistence; data paths; master volume; no menu resolver; invalid IDs rejected; missing hardware adapters fail explicitly.')
