#!/usr/bin/env python3
"""Ordinary ELF/objdump evidence, independent of unavailable REA MIPS provider."""
import argparse
import json
from pathlib import Path
import subprocess
from elftools.elf.elffile import ELFFile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    result={}
    for file in args.runtime.glob('*.so*'):
        with file.open('rb') as stream:
            elf=ELFFile(stream)
            result[file.name]=dict(needed=[t.needed for t in elf.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED'])
    command='/root/lilsmoke-os/build/buildroot/output/host/bin/mipsel-buildroot-linux-musl-objdump'
    for function in ('decoder_init','hcplayer_create','decoder_start','hcplayer_get_position','hcplayer_get_duration'):
        output=subprocess.check_output([command,'-d','--disassemble='+function,str(args.runtime/'libffplayer.so')],text=True)
        (args.out/(function+'.asm')).write_text(output)
    (args.out/'dependencies.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Saved bounded ordinary ELF evidence:',args.out)

if __name__=='__main__':main()
