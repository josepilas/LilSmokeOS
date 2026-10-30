#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build only the small musl compatibility helpers; no foreign engine rebuild."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SRC=ROOT/'package/lilsmoke-scripts/src'
UI=ROOT/'package/lilsmoke-ui/src/src'
SDK=Path(os.environ.get('SDK','/root/lilsmoke-os/build/buildroot/output'))


def build(out,target):
    out.mkdir(parents=True,exist_ok=True)
    source_files=[SRC/f for f in ('lilsmoke-treefrog-input.c','treefrog-common-input.cpp','treefrog-input-map.h','treefrog-loading.h','AppsProtocol.h')]
    source_files += [UI/f for f in ('Input.cpp','Input.h','Keymap.cpp','Keymap.h','Log.cpp','Log.h','gamepad-state.h','TreeFrogCatalog.cpp','TreeFrogCatalog.h')]
    source_files += [HERE/'preflight.cpp',HERE/'input_test.c',HERE/'loading_test.c',ROOT/'package/lilsmoke-od-sdl/src/input-wire.h']
    source_files += [ROOT/'package/lilsmoke-usb-gamepads/src/owner-lock.h']
    source_files += [ROOT/'tools/gamepads/test_owner_lock.c']
    source_files += [HERE/f for f in ('application.h','profile-input.cpp','classicube-input.h','classicube-input-test.c')]
    source_files += [HERE/'chocolate-doom-input.h']
    source_files += [SRC/'lilsmoke-treefrog-launch']
    source_files += [HERE/'application-probe.cpp']
    sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    c=str(SDK/'host/bin/mipsel-buildroot-linux-musl-gcc') if target=='mips' else 'cc'
    cxx=str(SDK/'host/bin/mipsel-buildroot-linux-musl-g++') if target=='mips' else 'c++'
    flags=['-O2','-Wall','-Wextra','-Werror','-I'+str(UI),'-I'+str(SRC),
        '-I'+str(ROOT/'package/lilsmoke-usb-gamepads/src')]
    if target=='mips':flags+=['-mips32r2','-mabi=32','-mfp32','-mhard-float']
    check=[cxx,'-std=c++17',*flags,'-I'+str(ROOT/'package/lilsmoke-apps/src'),
        str(HERE/'preflight.cpp'),str(UI/'TreeFrogCatalog.cpp'),'-ljson-c','-o',str(out/'lilsmoke-treefrog-check')]
    includes=['-I'+str(SDK/'staging/usr/include/SDL2')] if target=='mips' else subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True).split()
    bridge_object=out/'bridge.o'
    subprocess.run([c,'-std=c11',*flags,*includes,'-c',str(SRC/'lilsmoke-treefrog-input.c'),'-o',str(bridge_object)],check=True)
    wire=['-include',str(ROOT/'package/lilsmoke-od-sdl/src/input-wire.h')] if target=='mips' else []
    bridge=[cxx,'-std=c++17',*flags,*includes,*wire,str(bridge_object),
        str(HERE/'profile-input.cpp'),*[str(UI/f) for f in ('Input.cpp','Keymap.cpp','Log.cpp')],
        '-lSDL2','-o',str(out/'lilsmoke-treefrog-input')]
    mapping=[c,'-std=c11','-D_POSIX_C_SOURCE=200809L',*flags,str(HERE/'input_test.c'),'-o',str(out/'input-test')]
    loading=[c,'-std=c11',*flags,str(HERE/'loading_test.c'),'-o',str(out/'loading-test')]
    ownership=[c,'-std=c11',*flags,str(ROOT/'tools/gamepads/test_owner_lock.c'),'-o',str(out/'port-lease-test')]
    classicube=[c,'-std=c11',*flags,str(HERE/'classicube-input-test.c'),'-o',str(out/'classicube-input-test')]
    probe=[cxx,'-std=c++17',*flags,str(HERE/'application-probe.cpp'),str(UI/'TreeFrogCatalog.cpp'),
        '-ljson-c','-o',str(out/'application-probe')]
    for cmd in (check,bridge,mapping,loading,ownership,classicube,probe):subprocess.run(cmd,check=True)
    libraries=[]
    if target=='mips':
        for path in out.iterdir():
            if path.is_file() and path.name not in ('build.json','bridge.o'):
                info=subprocess.check_output([str(SDK/'host/bin/mipsel-buildroot-linux-musl-readelf'),'-d',str(path)],text=True)
                libraries.append(dict(file=path.name,dynamic=info))
    if sources!={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}:raise RuntimeError('Build inputs changed during compilation')
    report=dict(target=target,source_sha256=sources,binaries={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name!='build.json'},dependencies=libraries)
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report['binaries']),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',choices=['host','mips'],default='mips');p.add_argument('--out',type=Path)
    a=p.parse_args();build(a.out or HERE/'_build'/a.target,a.target)
