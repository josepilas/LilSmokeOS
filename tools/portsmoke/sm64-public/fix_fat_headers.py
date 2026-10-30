#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Keep unused case-colliding kernel networking headers outside the FAT runtime tree."""
import argparse
import json
from pathlib import Path
import shutil
import tarfile

def fix(app):
    app=app.resolve()
    if json.loads((app/'meta.json').read_text())['id']!='org.lilsmoke.sm64':
        raise ValueError('Not an SM64 development staging folder')
    with tarfile.open(app/'runtime/source.tar.gz','r:gz') as archive:
        for item in archive:
            if item.isfile() and Path(item.name).suffix in ('.c','.h','.cpp'):
                if b'#include <linux/netfilter' in archive.extractfile(item).read():
                    raise ValueError('An engine source needs the excluded networking headers')
    root=(app/'runtime/sysroot/usr/include/linux').resolve()
    if not root.is_relative_to(app/'runtime/sysroot') or root.is_symlink():
        raise ValueError('Unsafe staging header root')
    folders=[root/name for name in ('netfilter','netfilter_ipv4','netfilter_ipv6') if (root/name).is_dir()]
    archive_path=app/'runtime/corresponding-source/unused-netfilter-headers.tar.gz'
    if folders:
        with tarfile.open(archive_path,'w:gz') as archive:
            for folder in folders:
                if folder.is_symlink() or folder.resolve().parent!=root:
                    raise ValueError('Unsafe staging header folder')
                archive.add(folder,arcname='linux/'+folder.name)
        for folder in folders: shutil.rmtree(folder)
    return dict(fat_runtime_case_collisions_removed=True,unused_headers_retained_as_source=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(fix(args.app)))
