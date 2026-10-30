# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
root=Path(__file__).resolve().parents[2]
assert (root/'LICENSE').read_text().strip()==(root/'LICENSES/MPL-2.0.txt').read_text().strip()
assert 'Mozilla Public License Version 2.0' in (root/'LICENSES/MPL-2.0.txt').read_text()
assert (root/'package/lilsmoke-sm64-original/LICENSE').read_text().strip()==(root/'LICENSES/MPL-2.0.txt').read_text().strip()
print('PASS Alpha authored OS and separate SM64 MPL-2.0 text')
