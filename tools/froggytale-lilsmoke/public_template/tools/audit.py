#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas and contributors.
"""Independent source-tree inventory/privacy audit; no runner code or game inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re

IGNORED={'.git','_build','dist','__pycache__'}
MANIFEST='SOURCE-MANIFEST.json'
FORBIDDEN={'.win','.unx','.ios','.ogg','.wav','.pcm','.raw','.rgba4444','.exe','.dll',
           '.png','.jpg','.jpeg','.bmp','.gif','.webp','.lilpck','.zip','.o','.a','.so','.pyc'}
PATTERNS=[re.compile(p) for p in (
    rb'(?:[Cc]:[\\/]|/mnt/[a-z]/)Users[\\/][^\s"\']+',
    rb'/(?:root|home)/(?:lilsmoke[^\s"\']*|grand)(?:/|\\)',
    rb'gh[pousr]_[A-Za-z0-9]{25,}',rb'sk-proj-[A-Za-z0-9_-]{24,}',
    rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]


def files(root):
    return sorted(p for p in root.rglob('*') if p.is_file() and
                  not any(x in IGNORED for x in p.relative_to(root).parts) and p.name!=MANIFEST)


def scan(root,verify=True):
    entries={};problems=[]
    for path in files(root):
        rel=path.relative_to(root).as_posix();data=path.read_bytes()
        if path.is_symlink():problems.append(rel+': symlink not admitted')
        if path.suffix.lower() in FORBIDDEN or path.name.upper() in ('ATLAS.BIN','ATLAS.RGBA4444'):
            problems.append(rel+': asset/binary/archive extension')
        if rel.split('/')[0] in ('original','cache','saves') or path.name.startswith(('.env','PRIVATE')):
            problems.append(rel+': private input/output name')
        if b'\0' in data:problems.append(rel+': non-source binary content')
        if any(pattern.search(data) for pattern in PATTERNS):problems.append(rel+': owner path/credential material')
        entries[rel]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    if verify and (root/MANIFEST).is_file():
        if json.loads((root/MANIFEST).read_text())['files']!=entries:
            problems.append('source manifest changed')
    if problems:raise ValueError('\n'.join(problems))
    return dict(passed=True,files=entries,file_count=len(entries),bytes=sum(x['bytes'] for x in entries.values()),
                owner_inputs=False,game_assets=False,runtime_binaries=False,credentials=False,
                exclusions=sorted(IGNORED),manifest_excludes_itself=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--write-manifest',action='store_true')
    p.add_argument('--out',type=Path)
    a=p.parse_args();a.root=a.root.resolve()
    result=scan(a.root,verify=not a.write_manifest)
    if a.write_manifest:(a.root/MANIFEST).write_text(json.dumps(result,indent=2)+'\n')
    if a.out:a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))


if __name__=='__main__':main()
