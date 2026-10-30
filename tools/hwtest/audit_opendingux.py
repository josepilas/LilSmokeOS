#!/usr/bin/env python3
"""Inspect vendored build recipes without running downloaded source/scripts."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--out',type=Path,required=True); a=parser.parse_args()
    matrix=[]; recipes={}
    for directory in sorted((ROOT/'package/opendingux-emulators/src').iterdir()):
        if not directory.is_dir(): continue
        row={'name':directory.name,'status':'unverified','recipes':[],'concerns':[]}
        for name in ('Makefile','Makefile.libretro','Makefile.gcw0','CMakeLists.txt','configure','SConstruct'):
            file=directory/name
            if not file.is_file(): continue
            text=file.read_text(errors='replace')
            digest=hashlib.sha256(file.read_bytes()).hexdigest()
            row['recipes'].append(dict(file=name,sha256=digest,
                declared_targets=re.findall(r'(?m)^\s*(?:TARGET|TARGET_NAME)\s*[:?+]?=\s*(.+)$',text)[:12]))
            if name=='Makefile': recipes.setdefault(digest,[]).append(directory.name)
            if '_libretro' in text: row['concerns'].append(name+': includes libretro output; not proof of a standalone executable')
            if re.search(r'-DFAST_LSB_WORD_ACCESS\b',text): row['concerns'].append(name+': forces unaligned word accesses')
            if re.search(r'-D__arm__|-mfpu=neon|-march=arm',text): row['concerns'].append(name+': contains non-MIPS paths requiring per-target configuration')
            if re.search(r'GLES|OPENGL|OpenGL|glew',text): row['concerns'].append(name+': has graphics-API paths requiring a software-only build audit')
        if not row['recipes']: row['concerns'].append('no recognized top-level build recipe')
        matrix.append(row)
    result={'notice':'Static source/build audit, not game compatibility or successful builds.',
            'curated_source_ports':{'pocketsnes':'retrofw/pocketsnes 6fff59072821a68859d0d4d5caeec36d02ea264f',
                                   'pcsx4all':'separate reconstructed MIPS dynarec / native SDL2 package'},
            'identical_makefiles':[names for names in recipes.values() if len(names)>1],'vendored':matrix}
    a.out.parent.mkdir(parents=True,exist_ok=True); a.out.write_text(json.dumps(result,indent=2))
    print(f'OD_SOURCE_AUDIT trees={len(matrix)} duplicated_recipes={len(result["identical_makefiles"])}')
if __name__=='__main__': main()
