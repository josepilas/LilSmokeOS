"""BusyBox launcher regressions. No real engine, device or card is opened."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

P = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='lilsmoke-state-config-') as folder:
    root = Path(folder)
    (root / 'bin').mkdir()
    (root / 'cores').mkdir()
    (root / 'cores/fceumm_libretro.so').touch()
    for name, script in {
        'retroarch': '#!/bin/sh\nexit 0\n',
        'resolver': '#!/bin/sh\nprintf "%s\\n" "$2"\n',
        'mgba': '#!/bin/sh\nprintf "%s\\n" "$@" > "$QA_ARGS"\nprintf "%s\\n" "$LD_PRELOAD" > "$QA_PRELOAD"\n',
    }.items():
        path = root / 'bin' / name
        path.write_text(script)
        path.chmod(0o700)
    (root / 'Game.nes').write_bytes(b'launcher fixture, not a game')
    (root / 'user.cfg').write_text('quick_menu_show_save_load_state = false\nquick_menu_show_savestate_submenu = true\nsavestates_in_content_dir = true\nnotification_show_save_state = false\n')
    env = os.environ | {'PATH': str(root / 'bin') + ':/usr/bin:/bin',
        'LILSMOKE_CORES_DIR': str(root / 'cores'), 'LILSMOKE_EXTRA_CORES_DIR': str(root / 'extra'),
        'LILSMOKE_CONFIG': str(root / 'system.conf'),
        'LILSMOKE_RETROARCH_BASE_CONFIG': str(P / 'package/retroarch-mips/retroarch.cfg'),
        'LILSMOKE_RETROARCH_USER_CONFIG': str(root / 'user.cfg'),
        'LILSMOKE_RETROARCH_CORE_OPTIONS': str(root / 'options.cfg'),
        'LILSMOKE_RETROARCH_AUTOCONFIG': str(root / 'autoconfig'),
        'LILSMOKE_RETROARCH_PROFILES': str(root / 'profiles'),
        'LILSMOKE_RETROARCH_RUN_DIR': str(root / 'run'),
        'LILSMOKE_LOG_DIR': str(root / 'logs'),
        'LILSMOKE_ROM_RESOLVER': str(root / 'bin/resolver'),
        'LILSMOKE_LOADING_PROGRESS': str(root / 'progress'),
        'LILSMOKE_SESSION_LOG': str(root / 'no-session-log'),
    }
    for key, value in {'SAVE_ROOT': 'saves', 'STATE_ROOT': 'states',
                       'SCREENSHOT_DIR': 'screens', 'PLAYLIST_DIR': 'lists',
                       'THUMBNAIL_DIR': 'thumbnails', 'ARCHIVE_CACHE': 'cache'}.items():
        env['LILSMOKE_' + key] = str(root / value)
    for native in ('0', '1'):
        env['LILSMOKE_HCFB'] = native
        for arguments in (['menu'], ['run', 'nes', str(root / 'Game.nes')]):
            subprocess.run(['busybox', 'sh', str(P / 'package/lilsmoke-scripts/src/lilsmoke-retroarch'), *arguments], env=env, check=True)
            rows = re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', (root / 'run/retroarch.cfg').read_text(), re.M)
            cfg = dict(rows)
            assert len(rows) == len(cfg)
            for name, value in {'quick_menu_show_save_load_state': 'true',
                                'quick_menu_show_savestate_submenu': 'false',
                                'notification_show_save_state': 'true',
                                'sort_savestates_enable': 'true',
                                'sort_savestates_by_content_enable': 'true',
                                'savestates_in_content_dir': 'false',
                                'menu_savestate_resume': 'false'}.items():
                assert cfg[name] == value, (name, cfg[name])
            assert cfg['savestate_directory'] == '"' + str(root / 'states') + '"'
        assert (root / 'progress').read_text() == '70\tStarting emulator\n'
    env.update(LILSMOKE_HCFB='0', LILSMOKE_OD_HOME_ROOT=str(root / 'od'),
               LILSMOKE_OD_SHARE_ROOT=str(root / 'share'), LILSMOKE_EXTRA_OD_DIR=str(root / 'bin'),
               LILSMOKE_ROM_ROOT=str(root / 'roms'), QA_ARGS=str(root / 'args'), QA_PRELOAD=str(root / 'preload'))
    old = root / 'od/mgba/saves/Game.ss1'
    old.parent.mkdir(parents=True)
    old.write_bytes(b'previous genuine state placeholder')
    for arguments in (['mgba'], ['mgba', 'gba', str(root / 'Game.nes')]):
        subprocess.run(['busybox', 'sh', str(P / 'package/lilsmoke-scripts/src/lilsmoke-od-launch'), *arguments], env=env, check=True)
        args = (root / 'args').read_text().splitlines()
        assert 'savestatePath=' + str(root / 'od/mgba/states') in args
        assert 'savegamePath=' + str(root / 'od/mgba/saves') in args
        assert (root / 'od/mgba/states/Game.ss1').read_bytes() == old.read_bytes()
    # Test just the shell token-selection block without loading MIPS preloads
    # into the host shell or requiring fake files under the real /os directory.
    for script in ('package/lilsmoke-scripts/src/lilsmoke-od-launch', 'package/pcsx4all/app/PCSX4ALL.sh'):
        code = (P / script).read_text()
        block = code[code.index('LD_PRELOAD=\n'):code.index('if [ -r /usr/lib/lilsmoke/libod-sdl.so ]')]
        for game, token, expected in (('1', '/run/m8-boot-logo.state', '/os/libm8-boot-logo.so'),
                                      ('0', '/run/m8-boot-logo.state', ''), ('1', '/tmp/wrong-token', '')):
            result = subprocess.check_output(['busybox', 'sh', '-c', block + '\nprintf "%s" "$LD_PRELOAD"'],
                env=env | {'LILSMOKE_M8_LOADING_GAME': game, 'LILSMOKE_M8_BOOT_STATE': token}, text=True)
            assert result == expected, (script, result)
print('PASS: RA menu/run, M8/generic, stale visibility overrides, separate state paths, mGBA menu/run+migration, loading gate token, progress')
