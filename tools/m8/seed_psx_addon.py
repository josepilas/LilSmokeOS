#!/usr/bin/env python3
"""Put the qualified PSX addon in a generated image, never a physical disk."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from stock_format import inspect
from build_stock_candidate import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--addon',type=Path,required=True)
    a=p.parse_args()
    if a.image.is_symlink() or not a.image.is_file() or a.image.suffix!='.img':
        p.error('only a regular generated .img is accepted')
    report_path=a.image.with_suffix('.img.json')
    report=json.loads(report_path.read_text())
    if sha(a.image)!=report['image_sha256']:p.error('image differs from its generated report')
    layout=inspect(a.image)
    if [v['label'] for v in layout['partitions']]!=['LILOS','LILROMS']:p.error('not the generated two-partition layout')
    core=a.addon/'emulators/libretro/pcsx_rearmed_libretro.so'
    if sha(core)!='04bbc87bb1ceb9d93d81328eb185c1f6a2669d0aaabb6348e902123d6e48bfc4':p.error('unqualified PSX binary')
    offset=f"{a.image}@@{layout['partitions'][1]['offset_bytes']}"
    files=[]
    with tempfile.TemporaryDirectory(prefix='psx-image-readback-') as temporary:
        for file in sorted(a.addon.rglob('*')):
            if file.is_symlink():p.error('links are not accepted')
            if not file.is_file():continue
            relative=file.relative_to(a.addon).as_posix()
            if not re.fullmatch(r'emulators/(libretro/pcsx_rearmed_libretro\.so|info/pcsx_rearmed_libretro\.info|licenses/pcsx_rearmed/[A-Za-z0-9_.-]+)',relative):
                p.error('unallowlisted addon file: '+relative)
            for directory in reversed(file.relative_to(a.addon).parents):
                if directory==Path('.'):continue
                path='::/'+directory.as_posix()
                found=subprocess.run(['mdir','-i',offset,path],capture_output=True)
                if found.returncode:subprocess.run(['mmd','-i',offset,path],check=True)
            subprocess.run(['mcopy','-o','-i',offset,str(file),'::/'+relative],check=True)
            check=Path(temporary)/file.name
            subprocess.run(['mcopy','-o','-i',offset,'::/'+relative,str(check)],check=True)
            digest=sha(file)
            if sha(check)!=digest:raise RuntimeError('image addon readback mismatch: '+relative)
            files.append(dict(path=relative,sha256=digest,bytes=file.stat().st_size))
    if len(files)!=8:raise RuntimeError('expected the complete qualified binary/info/license payload')
    report['psx_addon']=dict(files=files,source='https://github.com/libretro/pcsx_rearmed',
                              commit='c8816799b50388e61cfe237fe2cdbb7d8175f20a',backend='Lightrec/MIPS, Unai software')
    report['image_sha256']=sha(a.image)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    a.image.with_suffix('.img.sha256').write_text(report['image_sha256']+'  '+a.image.name+'\n')
    print(json.dumps(dict(image_sha256=report['image_sha256'],psx_files_verified=len(files),physical_disk_access=False),indent=2))


if __name__=='__main__':main()
