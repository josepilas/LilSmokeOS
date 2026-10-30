#!/usr/bin/env python3
"""Timed game input and evidence capture into an explicitly named test VM."""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import time
p=argparse.ArgumentParser()
p.add_argument('folder',type=Path)
p.add_argument('--system',choices=['psx','n64'],required=True)
a=p.parse_args()
folder=a.folder.resolve()
sock=folder/'qmp.sock'
def qmp(name,arguments):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
        s.settimeout(60);s.connect(str(sock))
        with s.makefile('rwb',buffering=0) as f:
            json.loads(f.readline())
            for command in ({'execute':'qmp_capabilities'},{'execute':name,'arguments':arguments}):
                f.write(json.dumps(command).encode()+b'\n')
                while True:
                    reply=json.loads(f.readline())
                    if 'error' in reply: raise RuntimeError(reply)
                    if 'return' in reply: break
    with (folder/'manual-events.ndjson').open('a') as log:
        log.write(json.dumps(dict(wall_seconds=time.time(),action=command))+'\n')
def key(value,ms=1500):
    qmp('human-monitor-command',{'command-line':f'sendkey {value} {ms}'})
    time.sleep(ms/1000+1)
def capture(name):
    ppm=folder/(name+'.ppm')
    qmp('screendump',{'filename':str(ppm)})
    subprocess.run(['convert',str(ppm),str(folder/(name+'.png'))],check=True)
    print('CAPTURE',name,flush=True)
capture('04-title')
key('ret',4000)
time.sleep(40); capture('05-start')
key('ctrl' if a.system=='psx' else 'alt',3000)
time.sleep(40); capture('06-game-entry')
key('ctrl' if a.system=='psx' else 'alt',2500)
time.sleep(30); capture('07-game')
for i in range(3):
    key('up',2000)
    key('right',1500)
    key('ctrl' if a.system=='psx' else 'alt',1500)
    key('shift' if a.system=='psx' else 'ctrl',1500)
    key('left',1500)
capture('08-buttons')
key('ret',4000)
time.sleep(8); capture('09-start-pause')
key('ret',2000)
print('PERSONAL_GAME_INPUT_COMPLETE; visually verify states',flush=True)
