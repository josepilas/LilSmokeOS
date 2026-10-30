#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Collect exact Debian compiler dependency sources through signed APT metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE=Path(__file__).resolve().parent
P=HERE.parents[3]
WORK=Path('/root/lilsmoke-public-compiler')
OPTIONS=['-o','Dir::Etc::sourcelist='+str(HERE/'debian.sources'),
    '-o','Dir::Etc::sourceparts=-','-o','Dir::State::lists='+str(WORK/'apt/lists'),
    '-o','APT::Architecture=mipsel']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=WORK/'corresponding-source')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    archives=list(P.parents[2].glob('*_mipsel.deb'))+list((WORK/'debs').glob('*_mipsel.deb'))
    assert len(archives)>=17,'Compiler input packages missing'
    sources=set()
    package_rows=[]
    for archive in archives:
        fields=subprocess.check_output(['dpkg-deb','-f',str(archive),'Package','Version','Source'],text=True)
        metadata=dict(line.split(': ',1) for line in fields.splitlines() if ': ' in line)
        package,version=metadata['Package'],metadata['Version']
        source=metadata.get('Source',package)
        match=re.fullmatch(r'([^ ]+)(?: \(([^)]+)\))?',source)
        assert match,source
        source_name=match.group(1)
        source_version=match.group(2) or version
        sources.add(source_name+'='+source_version)
        package_rows.append(dict(package=package,version=version,source=source_name,
            source_version=source_version,sha256=hashlib.sha256(archive.read_bytes()).hexdigest()))
    for source in sorted(sources):
        print('Downloading exact corresponding source:',source,flush=True)
        subprocess.run(['apt-get',*OPTIONS,'source','--download-only',source],cwd=args.output,check=True)
    files={path.name:dict(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        for path in args.output.iterdir() if path.is_file() and path.name!='SOURCE-INDEX.json'}
    index=dict(repository='https://deb.debian.org/debian',distribution='bookworm',
        architecture='mipsel',apt_signed_metadata=True,packages=package_rows,files=files)
    (args.output/'SOURCE-INDEX.json').write_text(json.dumps(index,indent=2)+'\n')
    print(json.dumps(dict(source_packages=len(sources),files=len(files),
        bytes=sum(row['bytes'] for row in files.values())),indent=2))


if __name__=='__main__': main()
