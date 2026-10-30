# SPDX-License-Identifier: MPL-2.0
"""Label an existing development-image hardlink without producing a public release."""
import hashlib
import json
import os
from pathlib import Path

root=Path(__file__).resolve().parents[2]
source=root/'build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img'
target=root/'build/private-release/Pre-Alpha-0.0.1/LilSmokeOS-Pre-Alpha-0.0.1.img'
assert source.is_file() and target.is_file() and os.path.samefile(source,target)
report=json.loads(source.with_suffix('.img.json').read_text())
digest=hashlib.sha256()
with target.open('rb') as stream:
    for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
assert digest.hexdigest()==report['image_sha256']
report.update(release_name='Pre-Alpha-0.0.1',output=target.name,public_distribution_allowed=False,
    status='private-development-alias-not-public-release',hardlink_not_frozen_snapshot=True,
    corresponding_source_and_vendor_clearance_pending=True)
target.with_suffix('.img.json').write_text(json.dumps(report,indent=2)+'\n')
target.with_suffix('.img.sha256').write_text(digest.hexdigest()+'  '+target.name+'\n')
(target.parent/'DO-NOT-UPLOAD.txt').write_text(
    'LOCAL DEVELOPMENT IMAGE ONLY. This is a mutable hardlink alias, not a frozen release.\n'
    'Contains owner-imported stock boot dependencies. Redistribution/source clearance is pending.\n'
    'Do not publish on GitHub. Renaming does not change these restrictions.\n')
print('PASS private Pre-Alpha-0.0.1 image alias; bytes unchanged, no duplicate image storage')
