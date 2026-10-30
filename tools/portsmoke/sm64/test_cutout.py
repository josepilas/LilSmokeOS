#!/usr/bin/env python3
import argparse
from pathlib import Path
import subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--sdk',type=Path)
    p.add_argument('--sanitize',action='store_true')
    a=p.parse_args();configured=a.build/'tinygl-configured'
    source=Path(__file__).with_suffix('.cpp')
    cxx=str(a.sdk/'host/bin/mipsel-buildroot-linux-musl-g++') if a.sdk else 'g++'
    flags=['-std=c++17','-O2','-flto','-I'+str(configured/'include'),'-I'+str(configured/'src')]
    if a.sdk:flags+=['-static-libstdc++','-static-libgcc','-no-pie','-fno-pie','-mips32r2','-mplt','-mno-shared']
    if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    binary=a.build/'test-cutout'
    subprocess.run([cxx,*flags,str(source),*[str(x) for x in sorted(a.build.glob('*.o'))],'-lm','-o',str(binary)],check=True)
    command=[str(binary)]
    if a.sdk:command=['qemu-mipsel','-cpu','74Kf','-L',str(a.sdk/'staging'),*command]
    subprocess.run(command,check=True)

if __name__=='__main__':main()
