#!/usr/bin/env python3
"""Read diagnostic state from a test VM's explicit QMP socket."""
import argparse
import json
import socket
from pathlib import Path
import re
import time

p=argparse.ArgumentParser()
p.add_argument('socket')
p.add_argument('--command',default='info registers')
p.add_argument('--quit',action='store_true',help='terminate only the VM on this explicit socket')
p.add_argument('--key',help='send a key only into this test VM')
p.add_argument('--hold-ms',type=int,default=500)
p.add_argument('--screenshot',help='absolute path to a test VM PPM screenshot')
p.add_argument('--play-snes-level',action='store_true',help='from the selected SMW world-map level, enter and exercise run/jump/Start using timed VM keys')
p.add_argument('--restore-snes-jump',action='store_true',help='load the existing level state, then test jump/right/Start without host command latency')
a=p.parse_args()
if a.key and (not re.fullmatch(r'[a-z0-9_-]+',a.key) or not 1 <= a.hold_ms <= 5000):
    p.error('invalid VM key or duration')
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
    s.settimeout(15);s.connect(a.socket)
    with s.makefile('rwb',buffering=0) as f:
        json.loads(f.readline())
        def request(command):
            f.write(json.dumps(command).encode()+b'\n')
            while True:
                result=json.loads(f.readline())
                if 'error' in result: raise RuntimeError(result)
                if 'return' in result: return result['return']
        def record(action):
            with (Path(a.socket).resolve().parent/'manual-events.ndjson').open('a') as log:
                log.write(json.dumps(dict(wall_seconds=time.time(),action=action))+'\n')
        if a.play_snes_level or a.restore_snes_jump:
            if a.key or a.quit or (a.play_snes_level and a.restore_snes_jump): p.error('conflicting VM sequence options')
            request({'execute':'qmp_capabilities'})
            def key(value,hold):
                command={'execute':'human-monitor-command','arguments':{'command-line':f'sendkey {value} {hold}'}}
                request(command); record(command); time.sleep(hold/1000+0.2)
            if a.restore_snes_jump:
                key('f4',300)
                time.sleep(0.3)
                key('alt-right',1200)
            else:
                key('ctrl',600)
                time.sleep(3)
                for n in range(20):
                    key('shift-alt-right',350)
                    key('right',100)
            key('ret',400)
            command={'execute':'screendump','arguments':{'filename':a.screenshot or str(Path(a.socket).resolve().parent/'controlled-gameplay.ppm')}}
            request(command); record(command)
            print('VM_GAME_INPUT_SEQUENCE_CAPTURED; inspect image for actual game state')
            raise SystemExit(0)
        action=({'execute':'quit'} if a.quit else
            {'execute':'screendump','arguments':{'filename':a.screenshot}} if a.screenshot else
            {'execute':'human-monitor-command','arguments':{'command-line':f'sendkey {a.key} {a.hold_ms}' if a.key else a.command}})
        for command in ({'execute':'qmp_capabilities'},action):
            f.write(json.dumps(command).encode()+b'\n')
            while True:
                reply=json.loads(f.readline())
                if 'error' in reply:raise RuntimeError(reply)
                if 'return' in reply:break
        response=reply['return']
        if a.key and isinstance(response,str) and response.strip():
            raise RuntimeError('QMP rejected key: '+response.strip())
        print(response)
        if a.key or a.quit or a.screenshot:
            with (Path(a.socket).resolve().parent/'manual-events.ndjson').open('a') as log:
                log.write(json.dumps(dict(wall_seconds=time.time(),action=action))+ '\n')
        if a.key:
            # HMP returns before its release timer; do not replace a held key
            # with the next test command before the requested duration ends.
            time.sleep(a.hold_ms / 1000 + 0.2)
