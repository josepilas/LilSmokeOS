#!/usr/bin/env python3
"""Read-only stock uImage ABI literal survey; candidates are not runtime proof."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    data=args.image.read_bytes()
    if data[:4]!=b'\x27\x05\x19\x56':raise ValueError('Expected a stock uImage')
    size=struct.unpack_from('>I',data,12)[0]
    if data[31]!=1 or size>16*1024*1024:raise ValueError('Unsupported stock compression/size')
    kernel=zlib.decompress(data[64:64+size],31)
    if len(kernel)>16*1024*1024:raise ValueError('Excessive stock kernel')
    words=struct.unpack('<'+'I'*(len(kernel)//4),kernel[:len(kernel)//4*4])
    matches=[]
    for i,word in enumerate(words):
        if word>>26!=15 or not 0x8200<=word&65535<=0x8400:continue
        reg=word>>16&31
        for j in range(i+1,min(i+2,len(words))):
            next_word=words[j]
            if next_word>>26 not in (9,13) or next_word>>21&31!=reg:continue
            lo=next_word&65535
            if lo not in (0x0301,0x0400):continue
            imm=(word&65535)<<16
            value=(imm+(lo if next_word>>26==9 else 0)) if next_word>>26==9 else imm|lo
            matches.append(dict(file_offset=hex(i*4),request=hex(value),encoded_bytes=(value>>16)&8191,
                family='audio-init' if lo==0x301 else 'video-init',instructions=[hex(x) for x in words[i:j+1]]))
    args.out.mkdir(parents=True,exist_ok=False)
    (args.out/'survey.json').write_text(json.dumps(dict(image_sha256=hashlib.sha256(data).hexdigest(),
        kernel_bytes=len(kernel),candidates=matches,method='ordinary MIPS instruction literal survey, not execution'),indent=2)+'\n')
    print(json.dumps(matches,indent=2))

if __name__=='__main__':main()
