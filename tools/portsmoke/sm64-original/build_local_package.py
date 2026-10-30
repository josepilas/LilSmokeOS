"""Private local artifact from this user's dump. NEVER a public release package."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile
import argparse

HERE=Path(__file__).resolve().parent
P=HERE.parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
report=json.loads((HERE/'_build/mips-build.json').read_text())
assert not report['distributable']
binary=Path(report['privateBinary'])
data=binary.read_bytes();assert hashlib.sha256(data).hexdigest()==report['binarySha256']
source=Path(report['privateSource'])
meta=json.loads((P/'package/lilsmoke-sm64-original/local-app/meta.json').read_text())
parser=argparse.ArgumentParser()
parser.add_argument('--replace',action='store_true')
parser.add_argument('--out',type=Path,help='private package output directory')
args=parser.parse_args()
assert re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+',meta['version'])
output=(args.out or P/'build/private-local-apps')/f"Super-Mario-64-Local-Test-{meta['version']}.lilpck"
if output.exists():
    old=json.loads(output.with_suffix('.json').read_text())
    assert args.replace and old['private_user_build'] and not old['public_distribution_allowed']
    assert hashlib.sha256(output.read_bytes()).hexdigest()==old['package_sha256']
members={file.name:file.read_bytes() for file in (P/'package/lilsmoke-sm64-original/local-app').iterdir() if file.is_file()}
members['icon.webp']=(P/'package/portsmoke-sm64/src/icon.webp').read_bytes()
members['runtime/bin/sm64-original']=data
members['runtime/licenses/graphics-source-only.txt']=(source/'src/pc/gfx/LICENSE.txt').read_bytes()
members['runtime/licenses/LilSmoke-MPL-2.0.txt']=(P/'LICENSES/MPL-2.0.txt').read_bytes()
members['runtime/licenses/licensing-boundary.txt']=b'LilSmoke platform/menu/input additions: MPL-2.0. Upstream SM64 engine, renderer, extracted assets and bundled libraries retain their separate terms. This private package is not redistributable.\n'
members['runtime/licenses/SDL2.txt']=(B/'build/sdl2-2.28.5/LICENSE.txt').read_bytes()
members['PRIVATE-LOCAL-USE.txt']=b'Built locally from your personal dump. Contains extracted game data. Not a public BYO package. Do not redistribute this archive or its binary. The original ROM is not deleted.\n'
pending=[binary];seen=set()
while pending:
    current=pending.pop()
    dynamic=subprocess.check_output([str(B/'host/bin/mipsel-buildroot-linux-musl-readelf'),'-d',str(current)],text=True)
    for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic):
        if name=='libc.so' or name.startswith(('libdirectfb','libdirect-','libfusion-','libasound')) or name in seen:continue
        path=next((root/name for root in [B/'target/usr/lib',B/'target/lib'] if (root/name).is_file()),None)
        if path is None:raise RuntimeError('Missing library '+name)
        seen.add(name);members['runtime/lib/'+name]=path.read_bytes();pending.append(path)
members['runtime/lib/libusb-1.0.so.0']=(B/'target/usr/lib/libusb-1.0.so.0').read_bytes()
members['runtime/licenses/libusb.txt']=Path('/root/lilsmoke-hidapi-20261011/libusb-1.0.26/COPYING').read_bytes()
info=dict(private_user_build=True,public_distribution_allowed=False,contains_user_rom_assets=True,
          original_engine=True,binary_sha256=report['binarySha256'],rom_unchanged=report['romUnchanged'],
          source_pin=report['portPin'],base_pin=report['basePin'],source_folder='tools/portsmoke/sm64-original')
members['runtime/build-info.json']=(json.dumps(info,indent=2)+'\n').encode()
output.parent.mkdir(parents=True,exist_ok=True)
temporary=output.with_name(output.name+'.new')
with zipfile.ZipFile(temporary,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for name,data in sorted(members.items()):
        entry=zipfile.ZipInfo(name,date_time=(2026,10,11,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED
        entry.external_attr=(0o100755 if name.endswith('.sh') or name=='runtime/bin/sm64-original' else 0o100644)<<16
        archive.writestr(entry,data)
temporary.replace(output)
info.update(package=str(output),package_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),bytes=output.stat().st_size)
output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
print(json.dumps(info,indent=2))
