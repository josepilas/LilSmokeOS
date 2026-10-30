#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Historical Pre-Alpha documentation stager. Alpha uses stage_alpha_source.py."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
import re
from check_release import audit

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build/public-documentation/Pre-Alpha-0.0.1')
args=parser.parse_args()
if (ROOT/'VERSION').read_text(encoding='utf-8').strip() != 'Pre-Alpha-0.0.1':
    parser.error('Historical documentation-only stager disabled; use stage_alpha_source.py')
output=args.output.resolve()
if output.exists(): parser.error('Staging directory must be new')
selected={ROOT/p for p in ('README.md','LICENSE','LICENSE_POLICY.md','THIRD_PARTY_NOTICES.md',
    'CREDITS.md','CONTRIBUTING.md','CHANGELOG.md','VERSION','release.json','.gitignore',
    'docs/release-preparation.md','LICENSES/MPL-2.0.txt','LICENSES/LilSmokeOS-MIT-legacy.txt',
    'LICENSES/LilSmokeOS-PreAlpha-Personal.txt',
    'package/retroarch-mips/cores.lock','package/lilsmoke-libretro-extra/cores.lock')}
for directory in ('docs/pre-alpha-0.0.1','docs/developer','.github/ISSUE_TEMPLATE'):
    selected.update(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
for path in selected:
    if path.is_symlink() or not path.is_file(): raise ValueError('Non-regular public doc/source file')
    if audit([path])['passed'] is not True: raise ValueError('Selected file failed artifact review: '+path.name)
    text=path.read_text(encoding='utf-8')
    if re.search(r'(?i)(?:[A-Z]:[\\/]+Users[\\/]+[a-z0-9_ .-]+|/(?:root|home)/[a-z0-9_.-]+/)',text):
        raise ValueError('Private workstation path in public docs: '+str(path.relative_to(ROOT)))
output.mkdir(parents=True)
rows={}
for path in sorted(selected):
    relative=path.relative_to(ROOT).as_posix();destination=output/relative
    destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,destination)
    rows[relative]=dict(sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),bytes=destination.stat().st_size)
notice=dict(version='Pre-Alpha-0.0.1',kind='historical-pre-alpha-documentation-only',
    image_included=False,game_data_included=False,corresponding_source_for_binary=False,
    repository='https://github.com/josepilas/LilSmokeOS',manual_review_required=True,files=rows)
(output/'BUNDLE.json').write_text(json.dumps(notice,indent=2)+'\n',encoding='utf-8')
archive=output.parent/'LilSmokeOS-Pre-Alpha-0.0.1-documentation.zip'
if archive.exists(): raise ValueError('Documentation archive already exists')
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for path in sorted(output.rglob('*')):
        if path.is_file(): z.write(path,path.relative_to(output).as_posix())
result=audit([archive]);assert result['passed'],result
result.update(documentation_only=True,image_included=False,corresponding_source_for_binary=False,
    sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),files=len(rows)+1)
archive.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
