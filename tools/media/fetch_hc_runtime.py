#!/usr/bin/env python3
"""Fetch a pinned, isolated firmware ABI runtime for private device qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

REVISION='06eb2b99c4627a1200ecce188a80e029b60ed33e'
PREFIX='mipsel-buildroot-linux-gnu_sdk-buildroot/mipsel-buildroot-linux-gnu/sysroot/'
FILES={
    'lib/ld.so.1':'ld.so.1','lib/libc.so.6':'libc.so.6',
    'lib/libdl.so.2':'libdl.so.2','lib/libpthread.so.0':'libpthread.so.0',
    'lib/libm.so.6':'libm.so.6','lib/librt.so.1':'librt.so.1','lib/libgcc_s.so.1':'libgcc_s.so.1',
    'usr/lib/libffplayer.so':'libffplayer.so','usr/lib/libglist.so':'libglist.so',
    'usr/lib/libavcodec.so.58.134.100':'libavcodec.so.58',
    'usr/lib/libavformat.so.58.76.100':'libavformat.so.58',
    'usr/lib/libavutil.so.56.70.100':'libavutil.so.56',
    'usr/lib/libswscale.so.5.9.100':'libswscale.so.5',
    'usr/lib/crt1.o':'crt1.o','usr/lib/crti.o':'crti.o','usr/lib/crtn.o':'crtn.o',
    'usr/include/ffplayer.h':'ffplayer.h','usr/include/hcuapi/vidmp.h':'vidmp.h',
    'usr/include/hcuapi/auddec.h':'auddec.h','usr/include/hcuapi/iocbase.h':'iocbase.h',
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    records=[]
    for source,name in FILES.items():
        url='https://raw.githubusercontent.com/700zx1/sf3000toolchain/'+REVISION+'/'+PREFIX+source
        with urllib.request.urlopen(url,timeout=60) as response: data=response.read(24*1024*1024+1)
        if len(data)>24*1024*1024:raise ValueError('Excessive SDK artifact')
        if name.endswith(('.so','.so.1','.so.2','.so.6','.so.0','.so.58','.so.56','.so.5','.o')) and not data.startswith(b'\x7fELF'):
            raise ValueError('Expected ELF, not a symlink or HTTP document: '+source)
        (args.out/name).write_bytes(data)
        records.append(dict(source=source,file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        print('Fetched',name,len(data),flush=True)
    (args.out/'manifest.json').write_text(json.dumps(dict(revision=REVISION,files=records,private_qualification=True),indent=2)+'\n')

if __name__=='__main__':main()
