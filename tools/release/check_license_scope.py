# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import re

root=Path(__file__).resolve().parents[2]
own=('lilsmoke-ui','lilsmoke-apps','lilsmoke-fbtest','lilsmoke-scripts','lilsmoke-syspck',
    'lilsmoke-terminal','lilsmoke-usb-gamepads','lilsmoke-media','lilsmoke-od-sdl','lilsmoke-bluetooth','portsmoke')
for name in own:
    text=(root/'package'/name/(name+'.mk')).read_text()
    assert re.search(r'_LICENSE = MPL-2.0(?:\n|$)',text),name
    directory=root/'package'/name
    license_file=directory/('LICENSE' if name=='lilsmoke-od-sdl' else 'src/LICENSE')
    assert license_file.read_bytes()==(root/'LICENSES/MPL-2.0.txt').read_bytes(),name
upstream={'lilsmoke-gamepads':'MIT','lilsmoke-sdl12':'Zlib','lilsmoke-ffmpeg':'LGPL-2.1+',
    'lilsmoke-usb-transport':'LGPL-2.1+','lilsmoke-sqfs':'LGPL-3.0+'}
for name,expected in upstream.items():
    text=(root/'package'/name/(name+'.mk')).read_text()
    assert ('_LICENSE = '+expected) in text,name
assert 'MIT License' in (root/'LICENSES/LilSmokeOS-MIT-legacy.txt').read_text()
assert 'José Pilas' in (root/'LICENSE_POLICY.md').read_text(encoding='utf-8')
assert (root/'package/lilsmoke-sm64-original/LICENSE').read_text().strip()==(root/'LICENSES/MPL-2.0.txt').read_text().strip()
print('PASS Alpha authored MPL-2.0 and retained upstream/legacy terms')
