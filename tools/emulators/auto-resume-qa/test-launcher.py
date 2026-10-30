#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Opt-in RetroArch runtime-config regressions; BusyBox only, no device/engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

P = Path(__file__).resolve().parents[3]
LAUNCHER = P / 'package/lilsmoke-scripts/src/lilsmoke-retroarch'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    cases = []
    with tempfile.TemporaryDirectory(prefix='lilsmoke-auto-resume-') as tmp:
        root = Path(tmp)
        for folder in ('bin', 'cores', 'states'):
            (root / folder).mkdir()
        (root / 'cores/fceumm_libretro.so').touch()
        rom = root / 'A game with spaces.nes'
        rom.write_bytes(b'Launcher-only fixture; not a playable ROM')
        for name, source in {
            'retroarch': '#!/bin/sh\nprintf "%s\\n" "$@" > "$QA_ARGS"\nexit "${QA_EXIT:-0}"\n',
            'resolver': '#!/bin/sh\nprintf "%s\\n" "$2"\n',
            'reader': (P / 'package/lilsmoke-scripts/src/lilsmoke-config-read').read_text(),
            'bad-reader': '#!/bin/sh\nprintf "true\\n"\nexit 1\n',
        }.items():
            path = root / 'bin' / name
            path.write_text(source)
            path.chmod(0o700)
        (root / 'user.cfg').write_text('savestate_auto_save = true\nsavestate_auto_load = true\n'
            'savestate_auto_index = true\nsavestates_in_content_dir = true\n'
            'sort_savestates_enable = false\nsort_savestates_by_content_enable = false\nstate_slot = 17\n')
        (root / 'base.cfg').write_text('savestate_auto_save = false\nsavestate_auto_load = false\nstate_slot = 0\n')
        env = os.environ | dict(PATH=str(root / 'bin') + ':/usr/bin:/bin',
            LILSMOKE_CORES_DIR=str(root / 'cores'), LILSMOKE_EXTRA_CORES_DIR=str(root / 'extra'),
            LILSMOKE_CONFIG=str(root / 'system.conf'), LILSMOKE_DEFAULT_CONFIG=str(root / 'default.conf'),
            LILSMOKE_CONFIG_READER=str(root / 'bin/reader'),
            LILSMOKE_RETROARCH_BASE_CONFIG=str(root / 'base.cfg'), LILSMOKE_RETROARCH_USER_CONFIG=str(root / 'user.cfg'),
            LILSMOKE_RETROARCH_CORE_OPTIONS=str(root / 'options.cfg'),
            LILSMOKE_RETROARCH_AUTOCONFIG=str(root / 'autoconfig'), LILSMOKE_RETROARCH_PROFILES=str(root / 'profiles'),
            LILSMOKE_RETROARCH_RUN_DIR=str(root / 'run'), LILSMOKE_LOG_DIR=str(root / 'logs'),
            LILSMOKE_SESSION_LOG=str(root / 'not-installed'), LILSMOKE_ROM_RESOLVER=str(root / 'bin/resolver'),
            LILSMOKE_LOADING_PROGRESS=str(root / 'progress'), LILSMOKE_M8_AUDIO='0', QA_ARGS=str(root / 'args'))
        for key, value in dict(SAVE_ROOT='saves', STATE_ROOT='states', SCREENSHOT_DIR='screens',
                               PLAYLIST_DIR='lists', THUMBNAIL_DIR='thumbnails', ARCHIVE_CACHE='cache').items():
            env['LILSMOKE_' + key] = str(root / value)
        manual = root / 'states/manual.state17'
        manual.write_bytes(b'manual state unchanged by launcher\x00\xff')
        baseline = {}

        def launch(name, text, expected, mode='run', native='0', reader='reader', default='', exit_code=0,
                   config_present=True):
            if config_present:
                (root / 'system.conf').write_text(text)
            else:
                (root / 'system.conf').unlink(missing_ok=True)
            (root / 'default.conf').write_text(default)
            this_env = env | dict(LILSMOKE_HCFB=native, LILSMOKE_CONFIG_READER=str(root / 'bin' / reader), QA_EXIT=str(exit_code))
            command = ['busybox', 'sh', str(LAUNCHER), mode]
            if mode == 'run':
                command += ['nes', str(rom)]
            result = subprocess.run(command, env=this_env, text=True, capture_output=True, timeout=30)
            assert result.returncode == exit_code, result.stderr
            rows = re.findall(r'^\s*(\w+)\s*=\s*(.*?)\s*$', (root / 'run/retroarch.cfg').read_text(), re.M)
            cfg = dict(rows)
            assert len(rows) == len(cfg), 'Duplicate runtime keys'
            assert cfg['savestate_auto_save'] == cfg['savestate_auto_load'] == ('true' if expected else 'false')
            for key, value in dict(savestates_in_content_dir='false', sort_savestates_enable='true',
                sort_savestates_by_content_enable='true', savestate_auto_index='false', state_slot='17',
                quick_menu_show_save_load_state='true', menu_savestate_resume='false', menu_pause_libretro='true').items():
                assert cfg[key] == value, (key, cfg[key])
            assert cfg['savestate_directory'] == '"' + str(root / 'states') + '"'
            current = {k: v for k, v in cfg.items() if k not in ('savestate_auto_save', 'savestate_auto_load')}
            group = (mode, native)
            if group in baseline:
                assert current == baseline[group], 'Auto resume changed unrelated config/drivers/timing'
            else:
                baseline[group] = current
            argv = (root / 'args').read_text().splitlines()
            if mode == 'run':
                assert argv[-1] == str(rom) and argv[argv.index('-L') + 1] == str(root / 'cores/fceumm_libretro.so')
                assert 'auto_resume=' + str(expected).lower() in (root / 'logs/retroarch.log').read_text()
            else:
                assert '-L' not in argv
            assert manual.read_bytes() == b'manual state unchanged by launcher\x00\xff'
            assert not (root / 'executed').exists(), 'Configuration was evaluated as shell'
            cases.append(dict(name=name, mode=mode, native=native, auto_resume=expected, exit=result.returncode))

        for native in ('0', '1'):
            launch('missing-default-off', '', False, native=native)
            launch('absent-config-file-off', '', False, native=native, config_present=False)
            launch('default-file-opt-in', '', True, native=native, default='RETROARCH_AUTO_RESUME=true\n')
            for value in ('true', 'TRUE', '1', 'on', 'YES', '"true" # comment', "'true'", ' true   '):
                launch('opt-in-' + value, 'RETROARCH_AUTO_RESUME=' + value + '\n', True, native=native)
            for value in ('false', '0', 'off', 'no', '', 'maybe', '2', 'true; echo broken',
                          '$(touch ' + str(root / 'executed') + ')', 'true\\nvideo_driver=bad'):
                launch('off-or-malformed-' + value, 'RETROARCH_AUTO_RESUME=' + value + '\n', False, native=native)
            launch('last-system-key-wins', 'RETROARCH_AUTO_RESUME=true\nRETROARCH_AUTO_RESUME=false\n', False, native=native)
            launch('user-overrides-default', 'RETROARCH_AUTO_RESUME=false\n', False, native=native,
                   default='RETROARCH_AUTO_RESUME=true\n')
            launch('missing-reader-fails-closed', 'RETROARCH_AUTO_RESUME=true\n', False, native=native, reader='missing')
            launch('failed-reader-output-ignored', 'RETROARCH_AUTO_RESUME=true\n', False, native=native, reader='bad-reader')
            launch('menu-forced-off', 'RETROARCH_AUTO_RESUME=true\n', False, mode='menu', native=native)
            launch('menu-does-not-read-setting', 'RETROARCH_AUTO_RESUME=true\n', False, mode='menu', native=native, reader='bad-reader')
            launch('engine-exit-propagates', 'RETROARCH_AUTO_RESUME=true\n', True, native=native, exit_code=23)
        (root / 'system.conf').unlink()
        subprocess.run(['busybox', 'sh', '-n', str(LAUNCHER)], check=True)
        report = dict(passed=True, cases=cases, case_count=len(cases), shell='BusyBox sh',
            launcher_sha256=hashlib.sha256(LAUNCHER.read_bytes()).hexdigest(),
            manual_slot_preserved=True, unrelated_config_identical=True, configuration_not_evaluated=True,
            real_engine=False, card_writes=False)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))


if __name__ == '__main__':
    main()
