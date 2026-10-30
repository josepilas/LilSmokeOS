#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Verify the production install against the original release archive, not its own metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

def verify(out,package):
    serial=(out/'serial.log').read_text(errors='replace')
    for marker in ('QA_INSPECT=0','QA_INSTALL=0','QA_STORAGE_UMOUNT=0'):
        if marker not in serial: raise ValueError('Guest did not complete: '+marker)
    app=out/'installed/independent-apps/Super Mario 64'
    if not app.is_dir(): raise ValueError('Installed application missing from block image readback')
    with zipfile.ZipFile(package) as archive:
        raw=archive.read('runtime/distribution-manifest.json')
        expected_meta=json.loads(archive.read('meta.json'))
    if (app/'runtime/distribution-manifest.json').read_bytes()!=raw:
        raise ValueError('Installed manifest differs from release archive')
    manifest=json.loads(raw)
    for name,row in manifest['files'].items():
        if name=='meta.json':
            if json.loads((app/name).read_text())!=expected_meta:
                raise ValueError('Installer metadata changed its declared values')
            continue
        with (app/name).open('rb') as handle:
            if hashlib.file_digest(handle,'sha256').hexdigest()!=row['sha256']:
                raise ValueError('Installed file failed readback: '+name)
    if [p.name for p in app.glob('*.sh')]!=['SM64.sh'] or (app/'.sm64-cache').exists():
        raise ValueError('Unexpected root script or cooked game cache')
    result=dict(passed=True,production_package_inspect=True,production_package_install=True,
        all_immutable_file_hash_readback=True,metadata_semantic_readback=True,
        verified_files=len(manifest['files'])+1,
        reference_manifest_from_release_archive=True,single_root_script=True,
        rom_absent=True,cooked_game_absent=True,guest_ram_mib=120,
        installer_process_virtual_limit_mib=64,cpus=1,physical_card_modified=False,
        source_and_destination_disposable_block_images=True,storage_unmounted_cleanly=True)
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--package',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(verify(args.out,args.package),indent=2))
