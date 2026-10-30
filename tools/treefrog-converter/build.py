#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Pinned C dependencies and native/WASM builds. Never reads user ports."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request

HERE=Path(__file__).resolve().parent
DEPS={
    'miniz':('richgel999/miniz','2948eebafc5b9f1c69bebed80333e403a09bd62d',
             ['miniz.c','miniz.h','miniz_common.h','miniz_tdef.c','miniz_tdef.h','miniz_tinfl.c','miniz_tinfl.h','miniz_zip.c','miniz_zip.h','LICENSE']),
    'cjson':('DaveGamble/cJSON','6d9f2443ab071f86e5d9b43025a40929ec41c46c',['cJSON.c','cJSON.h','LICENSE'])}

def dependencies():
    inventory={}
    for name,(repo,commit,files) in DEPS.items():
        root=HERE/'vendor'/name;root.mkdir(parents=True,exist_ok=True)
        for f in files:
            p=root/f
            if not p.exists():
                data=urllib.request.urlopen(f'https://raw.githubusercontent.com/{repo}/{commit}/{f}',timeout=60).read()
                p.write_bytes(data)
            inventory[str(p.relative_to(HERE))]=hashlib.sha256(p.read_bytes()).hexdigest()
    export=HERE/'vendor/miniz/miniz_export.h'
    export.write_text('#define MINIZ_EXPORT\n')
    (HERE/'vendor/manifest.json').write_text(json.dumps(dict(pins=DEPS,sha256=inventory),indent=2)+'\n')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--wasm',action='store_true');ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--out',type=Path,default=HERE/'_build')
    ap.add_argument('--dist',type=Path,default=HERE/'site/dist')
    a=ap.parse_args()
    dependencies(); out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    sources=[HERE/'src/converter.c',HERE/'src/zip_modes.c',HERE/'vendor/cjson/cJSON.c',*[HERE/('vendor/miniz/'+s) for s in ('miniz.c','miniz_tinfl.c','miniz_tdef.c')]]
    common=['-std=c11','-O2','-ffile-prefix-map='+str(HERE)+'=.','-DMINIZ_NO_STDIO','-I'+str(HERE/'vendor/miniz'),'-I'+str(HERE/'vendor/cjson')]
    subprocess.run(['cc',*common,*map(str,sources),'-shared','-fPIC','-o',str(out/'libtfconvert.so')],check=True)
    subprocess.run(['cc',*common,*( ['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []),*map(str,sources),str(HERE/'src/main.c'),'-o',str(out/('tfconvert-asan' if a.sanitize else 'tfconvert'))],check=True)
    if a.wasm:
        dist=a.dist.resolve();dist.mkdir(parents=True,exist_ok=True)
        exports=['malloc','free','tf_open','tf_runtime','tf_report','tf_convert','tf_output_data','tf_output_size','tf_error','tf_close']
        subprocess.run(['emcc',*common,*map(str,sources),'-O3','-sDYNAMIC_EXECUTION=0','-sMODULARIZE=1','-sEXPORT_NAME=createTreeFrogConverter','-sENVIRONMENT=worker,node','-sFILESYSTEM=0','-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=33554432','-sMAXIMUM_MEMORY=1610612736','-sEXPORTED_FUNCTIONS='+json.dumps(['_'+s for s in exports]),'-sEXPORTED_RUNTIME_METHODS='+json.dumps(['UTF8ToString','stringToUTF8','lengthBytesUTF8']),'-o',str(dist/'converter.js')],check=True)
        licenses=dist/'licenses';licenses.mkdir(exist_ok=True)
        for name in DEPS:shutil.copyfile(HERE/'vendor'/name/'LICENSE',licenses/(name+'.txt'))
    print(json.dumps(dict(native=str(out/('tfconvert-asan' if a.sanitize else 'tfconvert')),wasm=a.wasm)))
if __name__=='__main__':main()
