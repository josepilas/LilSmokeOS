"""Exercise the BusyBox launcher; no game, card or physical device is opened."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

P = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='lilsmoke-analog-config-') as folder:
    root = Path(folder)
    (root / 'bin').mkdir()
    (root / 'cores').mkdir()
    for name in ('mgba', 'parallel_n64'):
        (root / 'cores' / (name + '_libretro.so')).touch()
    for name, script in {
        'retroarch': '#!/bin/sh\nexit 0\n',
        'resolver': '#!/bin/sh\nprintf "%s\\n" "$2"\n',
    }.items():
        file = root / 'bin' / name
        file.write_text(script)
        file.chmod(0o700)
    (root / 'game.gba').write_bytes(b'private synthetic fixture')
    (root / 'system.conf').write_text('DISPLAY_WIDTH=1280\nDISPLAY_HEIGHT=720\n')
    (root / 'base.cfg').write_text('input_player1_l_x_plus_axis = "nul"\n')
    (root / 'user.cfg').write_text('input_player1_analog_dpad_mode = 0\ninput_player2_analog_dpad_mode = 2\n')
    env = os.environ | {
        'PATH': str(root / 'bin') + ':/usr/bin:/bin',
        'LILSMOKE_HCFB': '1', 'LILSMOKE_CONFIG': str(root / 'system.conf'),
        'LILSMOKE_CORES_DIR': str(root / 'cores'),
        'LILSMOKE_EXTRA_CORES_DIR': str(root / 'extra'),
        'LILSMOKE_RETROARCH_BASE_CONFIG': str(root / 'base.cfg'),
        'LILSMOKE_RETROARCH_USER_CONFIG': str(root / 'user.cfg'),
        'LILSMOKE_RETROARCH_CORE_OPTIONS': str(root / 'options.cfg'),
        'LILSMOKE_RETROARCH_AUTOCONFIG': str(root / 'autoconfig'),
        'LILSMOKE_RETROARCH_PROFILES': str(root / 'profiles'),
        'LILSMOKE_RETROARCH_RUN_DIR': str(root / 'run'),
        'LILSMOKE_LOG_DIR': str(root / 'logs'),
        'LILSMOKE_ROM_RESOLVER': str(root / 'bin' / 'resolver'),
    }
    for key, value in {'SAVE_ROOT': 'saves', 'STATE_ROOT': 'states',
                       'SCREENSHOT_DIR': 'screens', 'PLAYLIST_DIR': 'lists',
                       'THUMBNAIL_DIR': 'thumbnails', 'ARCHIVE_CACHE': 'cache'}.items():
        env['LILSMOKE_' + key] = str(root / value)
    launcher = P / 'package/lilsmoke-scripts/src/lilsmoke-retroarch'
    for arguments in (['menu'], ['run', 'gba', str(root / 'game.gba')],
                      ['run', 'n64', str(root / 'game.gba')]):
        subprocess.run(['busybox', 'sh', str(launcher), *arguments], env=env, check=True)
        rows = re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', (root / 'run/retroarch.cfg').read_text(), re.M)
        cfg = dict(rows)
        assert len(rows) == len(cfg), 'duplicate first-definition config keys'
        assert cfg['input_joypad_driver'] == '"lilsmoke"'
        assert cfg['input_autodetect_enable'] == 'false'
        for player in range(1, 5):
            for axis, name in enumerate(('l_x', 'l_y', 'r_x', 'r_y')):
                for sign, label in (('-', 'minus'), ('+', 'plus')):
                    assert cfg[f'input_player{player}_{name}_{label}_axis'] == f'"{sign}{axis}"'
            assert cfg[f'input_player{player}_analog_dpad_mode'] == str({1: 0, 2: 2}.get(player, 1))
        assert cfg['input_menu_toggle_btn'] == '"16"' and cfg['input_exit_emulator_btn'] == '"17"'
    env['LILSMOKE_HCFB'] = '0'
    subprocess.run(['busybox', 'sh', str(launcher), 'menu'], env=env, check=True)
    assert 'input_player1_l_x_plus_axis = "nul"' in (root / 'run/retroarch.cfg').read_text()
print('PASS: both stick axes on four native ports; menu/GBA/N64; saved D-pad modes; no duplicate keys; generic profile preserved')
