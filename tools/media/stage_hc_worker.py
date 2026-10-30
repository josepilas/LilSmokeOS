#!/usr/bin/env python3
"""Stage one isolated stock-firmware dependency closure, never the SDK/OS tree."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from elftools.elf.elffile import ELFFile

P=Path(__file__).resolve().parents[2]
SDK=Path('/root/lilsmoke-os/build/buildroot/output')
STOCK=P/'build/r36sx-2.7-20261023-diagnostics/stock-prepared-02/extracted/usbimager-20260530T0359/rootfs'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--crt',type=Path,default=Path('/root/lilsmoke-hcplayer-20261024-sdk'))
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    pending=['libffplayer.so','libdl.so.2','libgcc_s.so.1'];done={};manifest=[]
    while pending:
        name=pending.pop()
        if name in done:continue
        source=next((STOCK/base/name for base in ('lib','usr/lib') if (STOCK/base/name).is_file()),None)
        if source is None:raise ValueError('Missing native runtime dependency: '+name)
        data=source.read_bytes()
        if not data.startswith(b'\x7fELF'):raise ValueError('Not native ELF: '+name)
        with source.open('rb') as stream:
            elf=ELFFile(stream)
            if elf.elfclass!=32 or not elf.little_endian or elf.header.e_machine!='EM_MIPS':raise ValueError('Wrong ABI')
            needed=[t.needed for t in elf.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED']
        pending.extend(needed);done[name]=source
        shutil.copyfile(source,args.out/name)
        subprocess.run([str(SDK/'host/bin/mipsel-buildroot-linux-musl-strip'),'--strip-unneeded',str(args.out/name)],check=True)
        if name=='ld.so.1':(args.out/name).chmod(0o755)
        manifest.append(dict(file=name,stock_sha256=hashlib.sha256(data).hexdigest(),needed=needed,
            staged_sha256=hashlib.sha256((args.out/name).read_bytes()).hexdigest()))
    for name in ('crt1.o','crti.o','crtn.o'):shutil.copyfile(args.crt/name,args.out/name)
    compiler=SDK/'host/bin/mipsel-buildroot-linux-musl-gcc'
    source=P/'package/lilsmoke-media/src/hcplayer_worker.c'
    subprocess.run(['nice','-n','10','taskset','-c','0','prlimit','--as=805306368','--',str(compiler),
        '-std=gnu11','-O2','-mips32r2','-mabi=32','-mfp32','-mhard-float',
        '-fno-stack-protector','-fno-pie','-no-pie','-nostdlib','-nostartfiles',
        '-Wl,--export-dynamic','-Wl,-dynamic-linker,/usr/lib/lilsmoke/hcplayer/ld.so.1',
        str(args.out/'crt1.o'),str(args.out/'crti.o'),str(source),'-L'+str(args.out),
        '-Wl,-rpath-link,'+str(args.out),'-Wl,--no-as-needed','-l:libdl.so.2','-l:libc.so.6','-l:ld.so.1',
        '-lgcc',str(args.out/'crtn.o'),'-o',str(args.out/'hcplayer-worker')],check=True)
    manifest.append(dict(file='hcplayer-worker',source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        staged_sha256=hashlib.sha256((args.out/'hcplayer-worker').read_bytes()).hexdigest()))
    (args.out/'manifest.json').write_text(json.dumps(dict(profile='HC16xx-video-664-audio-disabled',
        private_stock_runtime=True,files=manifest),indent=2)+'\n')
    print('Staged isolated firmware runtime; requires Linux/MIPS probe:',args.out)

if __name__=='__main__':main()
