#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only, allowlisted Windows card snapshot for the Alpha file image builder."""
import argparse,hashlib,json,os,shutil
from pathlib import Path
from build_alpha_image import ROOT,BASELINE,rows,relative

def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--card',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--resume',action='store_true');a=ap.parse_args()
    source=a.card.resolve(strict=True);out=a.out.resolve()
    if out.is_relative_to(source) or (os.name=='nt' and out.drive.lower()==source.drive.lower()):raise ValueError('Snapshot must be on another PC volume, not the card')
    if out.exists() and not a.resume:raise ValueError('Use a NEW snapshot directory or explicit --resume')
    if a.resume and (out.name!='card-snapshot' or not out.is_relative_to(ROOT/'build/release-upload/Alpha-0.0.1')):raise ValueError('Resume is limited to the generated Alpha snapshot')
    if os.name=='nt':out=Path('\\\\?\\'+str(out))
    manifest=source/'lilsmoke/runtime.tsv'
    if digest(manifest)!=BASELINE:raise ValueError('Unexpected source runtime')
    table=rows(manifest.read_text(encoding='ascii'))
    files={'lilsmoke/runtime.tsv','cubegm/icube.stock','cubegm/icube'}
    files.update('lilsmoke/files/'+r[3] for r in table if r[0] in ('F','C'))
    files.update('lilsmoke/'+n for n in ('initial.conf','layout.conf','m8-launch.sh','m8-probe','m8-grow','m8-boot-logo','libm8-boot-logo.so','libm8-display.so','boot-logo.argb'))
    profile=json.loads((ROOT/'tools/device-adapter/profiles/segam-m8-v6.json').read_text())
    files.update('cubegm/'+n for n in profile['files'] if (source/'cubegm'/n).is_file())
    delivery=json.loads((ROOT/'tools/device-adapter/release/lilos/delivery.json').read_text())
    files.update(delivery['files'])
    out.mkdir(parents=True,exist_ok=a.resume)
    inventory={}
    for index,name in enumerate(sorted(files)):
        relative(name);src=source/name;dst=out/name
        if src.is_symlink() or not src.is_file():raise ValueError('Unexpected card file: '+name)
        dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.is_file() or digest(dst)!=digest(src):shutil.copyfile(src,dst)
        before=digest(src);after=digest(dst)
        if before!=after:raise ValueError('Snapshot readback mismatch: '+name)
        inventory[name]={'sha256':after,'bytes':dst.stat().st_size}
        if index%100==0:print('Verified',index,'of',len(files),flush=True)
    if digest(manifest)!=BASELINE:raise ValueError('Card changed while reading')
    report={'passed':True,'physical_card_modified':False,'baseline':BASELINE,'files':inventory,'bytes':sum(r['bytes'] for r in inventory.values())}
    (out/'snapshot.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':True,'files':len(inventory),'bytes':report['bytes']}))
if __name__=='__main__':main()
