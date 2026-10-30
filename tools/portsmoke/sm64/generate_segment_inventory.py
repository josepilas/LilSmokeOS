#!/usr/bin/env python3
"""Emit only pinned offset/size metadata; never emit ROM bytes or game assets."""
import argparse
import hashlib
import json
from pathlib import Path
from generate_manifest import SOURCE_SHA256, REVISION, convert
from test_private_rom import mio0

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();metadata=a.assets.read_bytes()
    assert hashlib.sha256(metadata).hexdigest()==SOURCE_SHA256
    raw=a.rom.read_bytes();assert hashlib.sha1(raw).hexdigest()=='9bef1128717f958171a4afac3ed78ee2bb4e86ce'
    offsets={row[1] for row in convert(json.loads(metadata)) if row[1]>=0}|{4178608,3330160,1132368}
    rows=[]
    for offset in sorted(offsets):
        data=mio0(raw,offset)
        assert 0<len(data)<=4*1024*1024
        rows.append([f'segments/mio0-{offset:08x}.bin',offset,0,len(data),0,0,0])
    identity=hashlib.sha1(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
    lines=['// Offset/size metadata only; no ROM or extracted bytes.',f'// n64decomp/sm64 {REVISION}',
           f'constexpr const char *segmentIdentity = "{identity}";', 'constexpr AssetSpec segmentSpecs[] = {']
    for name,block,offset,size,w,h,fmt in rows:lines.append(f'    {{"{name}", {block}, {offset}, {size}, {w}, {h}, {fmt}}},')
    lines.append('};\n');a.output.write_text('\n'.join(lines),encoding='ascii')
    print(f'Generated {len(rows)} segment descriptors, {sum(row[3] for row in rows)} private decoded bytes; no data emitted')

if __name__=='__main__':main()
