"""Package the already-built native PCSX4ALL unchanged; no ROM or BIOS assets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

P=Path(__file__).resolve().parents[2]
B=Path('/root/lilsmoke-os/build/buildroot/output')
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary',type=Path,default=B/'target/usr/bin/pcsx4all-launch')
p.add_argument('--target-root',type=Path,default=B/'target')
p.add_argument('--readelf',type=Path,default=B/'host/bin/mipsel-buildroot-linux-musl-readelf')
p.add_argument('--output',type=Path)
p.add_argument('--replace',action='store_true',help='replace only a verified previous generated package')
p.add_argument('--core-archive',type=Path,help='archive actually used to link the frontend, for provenance')
p.add_argument('--require-qualified-core',action='store_true',help='refuse any core other than the pinned audio-qualified archive')
a=p.parse_args()
meta=json.loads((P/'package/pcsx4all/app/meta.json').read_text())
if a.output is None:a.output=P/'build/lilsmoke-applications'/('PCSX4ALL-'+meta['version']+'.lilpck')
data=a.binary.read_bytes()
assert data[:6]==b'\x7fELF\x01\x01' and int.from_bytes(data[18:20],'little')==8,'Not a MIPS32 little-endian ELF'
if meta['version'] in ('1.0.4', '1.0.5', '1.0.6'):
    assert all(marker in data for marker in (b'PCSX_STATE_SAVE', b'PCSX_STATE_LOAD', b'Save state', b'Load state')), 'State-capable frontend required'
if meta['version'] in ('1.0.5', '1.0.6'):
    assert all(marker in data for marker in (b'PCSX_STATE_IO_ERROR', b'PCSX_STATE_SYNC_FALLBACK', b'PCSX_STATE_ENGINE_ERROR')), 'Recovery-capable frontend required'
    assert a.core_archive, 'Recovery release requires linked core provenance'
if meta['version']=='1.0.6':
    assert b'Storage read-only; repair SD' in data, 'Storage-aware frontend required'
members={}
for file in (P/'package/pcsx4all/app').iterdir():
    if file.is_file():members[file.name]=file.read_bytes()
members['runtime/bin/pcsx4all']=data
members['runtime/defaults/pcsx4all.cfg']=(P/'package/pcsx4all/pcsx4all.cfg').read_bytes()
members['runtime/licenses/PCSX4ALL-gpl.txt']=(P/'package/pcsx4all/src/gpl.txt').read_bytes()
libraries={};pending=[a.binary]
while pending:
    current=pending.pop()
    text=subprocess.check_output([str(a.readelf),'-d',str(current)],text=True)
    for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]',text):
        # These belong to the OS/device platform, not the portable application.
        if name=='libc.so' or name.startswith(('libdirectfb','libdirect-','libfusion-','libasound','libstdc++','libgcc_s')) or name in libraries:continue
        source=next((root/name for root in [a.target_root/'usr/lib',a.target_root/'lib'] if (root/name).is_file()),None)
        if source is None:raise RuntimeError('Missing runtime library: '+name)
        libraries[name]=source;pending.append(source)
        members['runtime/lib/'+name]=source.read_bytes()
# Retain the actual source license texts available in the qualified SDK build.
license_roots={'sdl2':'sdl2-*','sdl2_ttf':'sdl2_ttf-*','libpng':'libpng-*','zlib':'zlib-*','freetype':'freetype-*',
               'harfbuzz':'harfbuzz-*','bzip2':'bzip2-*','brotli':'brotli-*','expat':'expat-*','fontconfig':'fontconfig-*'}
for package,pattern in license_roots.items():
    for folder in sorted((a.target_root.parent/'build').glob(pattern)):
        for relative in ['LICENSE','LICENSE.txt','COPYING','README','docs/FTL.TXT','docs/GPLv2.TXT']:
            file=folder/relative
            if file.is_file():members['runtime/licenses/'+package+'-'+relative.replace('/','-')]=file.read_bytes()
report=dict(id=meta['id'],version=meta['version'],core_binary_sha256=hashlib.sha256(data).hexdigest(),
            native_engine_unchanged=hashlib.sha256(data).hexdigest()=='e515972180668659fa82e3219e3164fc2bd9d4a60819743b54aec29138b7ec17',
            input_frontend_updated=meta['version'] in ('1.0.2', '1.0.3', '1.0.4', '1.0.5', '1.0.6'),rom_or_bios_included=False,
            bundled_libraries={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in libraries.items()},
            source='package/pcsx4all/src',platform_dependencies=['musl','libstdc++','libgcc','DirectFB','device adapters'])
if a.core_archive:
    core_hash=hashlib.sha256(a.core_archive.read_bytes()).hexdigest()
    report.update(engine_archive_sha256=core_hash,
                  qualified_psx_archive_preserved=core_hash=='1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a')
report['save_state_slots']=10 if meta['version'] in ('1.0.4', '1.0.5', '1.0.6') else None
if meta['version'] in ('1.0.5', '1.0.6'):
    report['save_state_io']='staged-gzip-null-mapped-jit-ram;checked-sync-fallback'
if meta['version']=='1.0.6':
    report['storage_failure_diagnostics']='read-only;full;permissions;I/O'
if a.require_qualified_core:
    assert report.get('qualified_psx_archive_preserved'), 'Refusing unqualified core for recovery release'
members['runtime/build-info.json']=(json.dumps(report,indent=2)+'\n').encode()
a.output.parent.mkdir(parents=True,exist_ok=True)
if a.output.exists():
    assert a.replace,'Refusing to overwrite an existing package'
    previous=json.loads(a.output.with_suffix('.json').read_text())
    assert previous['id']=='org.lilsmoke.pcsx4all' and previous['package_sha256']==hashlib.sha256(a.output.read_bytes()).hexdigest(),'Existing file is not a verified generated package'
temporary=a.output.with_name(a.output.name+'.new')
with zipfile.ZipFile(temporary,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for name,data in sorted(members.items()):
        entry=zipfile.ZipInfo(name,date_time=(2026,10,11,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED
        entry.external_attr=(0o100755 if name.endswith('.sh') or name=='runtime/bin/pcsx4all' else 0o100644)<<16
        archive.writestr(entry,data)
assert len([name for name in members if '/' not in name and name.endswith('.sh')])==1
temporary.replace(a.output)
report.update(package=str(a.output),package_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),files=len(members),bytes=a.output.stat().st_size)
a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
