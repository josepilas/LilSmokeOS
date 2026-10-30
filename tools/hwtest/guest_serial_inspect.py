#!/usr/bin/env python3
"""Read guest diagnostics through a VM's explicit QMP/serial sockets only."""
import argparse
import json
from pathlib import Path
import socket
import time
p=argparse.ArgumentParser()
p.add_argument('folder',type=Path)
p.add_argument('--command',default='cat /tmp/lilsmoke-retroarch/retroarch.cfg')
a=p.parse_args()
folder=a.folder.resolve(); serial=folder/'inspect-serial.sock'
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
    s.settimeout(30); s.connect(str(folder/'qmp.sock'))
    with s.makefile('rwb',buffering=0) as f:
        json.loads(f.readline())
        for cmd in [{'execute':'qmp_capabilities'},{'execute':'chardev-change','arguments':{
            'id':'serial0','backend':{'type':'socket','data':{
                'addr':{'type':'unix','data':{'path':str(serial)}},'server':True,'wait':False}}}}]:
            f.write(json.dumps(cmd).encode()+b'\n')
            while True:
                response=json.loads(f.readline())
                if 'error' in response: raise RuntimeError(response)
                if 'return' in response: break
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
    s.settimeout(2); s.connect(str(serial))
    s.sendall((a.command+'\n').encode())
    result=bytearray(); deadline=time.monotonic()+12
    while time.monotonic()<deadline:
        try:
            chunk=s.recv(65536)
            if not chunk: break
            result.extend(chunk)
        except socket.timeout:
            if result: break
    text=result.decode(errors='replace')
    print(text)
    with (folder/'guest-inspection.log').open('a') as log:
        log.write('\nCOMMAND '+a.command+'\n'+text)
