#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Package owned frontends and audited library entries only, never SDK globbing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent

def elf(path):
    def read(*args):
        return subprocess.check_output(['readelf', *args, str(path)], text=True)
    headers, dynamic, programs, attributes = read('-h'), read('-d'), read('-l'), read('-A')
    needed = re.findall(r'\(NEEDED\).*?\[([^]]+)\]', dynamic)
    interpreter = re.search(r'Requesting program interpreter: ([^]]+)', programs)
    return dict(needed=needed, interpreter=interpreter.group(1) if interpreter else None,
                header=headers, attributes=attributes, versions=read('-V'))

def pack(args):
    entries = {}
    manifest = dict(schema=1, name='lilretro', version='0.2.0', source_license='MPL-2.0',
                    copyright='Copyright (c) 2026 Jose Pilas',
                    standalone_frontend=True, includes_original_picoarch=False,
                    self_contained=False, platform_driver_policy='OS-provided board ABI only', profiles={}, files={})
    def add(name, data, executable=False, metadata=None):
        if name.startswith('/') or '..' in Path(name).parts or name in entries:
            raise ValueError('Invalid/duplicate archive path: ' + name)
        entries[name] = (data, executable)
        manifest['files'][name] = dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), **(metadata or {}))
    for abi in ('musl', 'glibc'):
        directory = args.build_root / (abi + args.variant)
        frontend = directory / 'lilretro'
        if not frontend.exists():
            if args.require_both:
                raise ValueError('Missing frontend: ' + abi)
            continue
        build = json.loads((directory / 'build.json').read_text())
        info = elf(frontend)
        if 'MIPS' not in info['header'] or 'ELF32' not in info['header'] or 'little endian' not in info['header']:
            raise ValueError('Wrong frontend ABI')
        add(abi + '/lilretro', frontend.read_bytes(), True, dict(license='MPL-2.0', elf=info))
        add('source/build-' + abi + '.json', (directory / 'build.json').read_bytes())
        test_report = directory / 'test-results.json'
        if test_report.exists():
            add('qa/' + abi + '.json', test_report.read_bytes())
        manifest['profiles'][abi] = dict(abi=build['abi'], frontend=abi+'/lilretro',
            interpreter=info['interpreter'], frontend_needed=info['needed'],
            bundled_libraries=[], platform_required=list(info['needed']),
            closure_status='requires matching OS platform namespace; not self-contained',
            hardware_qualified=False)
        # Describe the external transitive closure without redistributing it.
        sdk = getattr(args, abi + '_sysroot')
        pending=list(info['needed']);seen=set();external={}
        while pending:
            soname=pending.pop()
            if soname in seen:
                continue
            seen.add(soname)
            path=next((sdk/d/soname for d in ('lib','usr/lib') if (sdk/d/soname).is_file()),None) if sdk else None
            if path is None:
                external[soname]=dict(provided_by='OS platform',status='not available in build sysroot')
                continue
            libinfo=elf(path)
            external[soname]=dict(provided_by='OS platform',redistributed=False,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(),needed=libinfo['needed'])
            pending.extend(libinfo['needed'])
        manifest['profiles'][abi]['external_libraries']=external
        manifest['profiles'][abi]['platform_required']=sorted(seen)
    if args.libraries:
        for lib in json.loads(args.libraries.read_text()):
            required = ('abi', 'soname', 'path', 'sha256', 'license', 'source_url', 'corresponding_source_verified')
            if any(k not in lib for k in required) or not lib['corresponding_source_verified']:
                raise ValueError('Library lacks verified licensing/corresponding source')
            if lib['abi'] not in manifest['profiles'] or not re.fullmatch(r'[A-Za-z0-9_.+-]+',lib['soname']):
                raise ValueError('Invalid library ABI/SONAME')
            data = Path(lib['path']).read_bytes()
            if hashlib.sha256(data).hexdigest() != lib['sha256']:
                raise ValueError('Library hash mismatch')
            info = elf(Path(lib['path']))
            add(lib['abi']+'/lib/'+lib['soname'],data,metadata=dict(license=lib['license'],source_url=lib['source_url'],elf=info))
            manifest['profiles'][lib['abi']]['bundled_libraries'].append(lib['soname'])
    source_files = ['lilretro.c', 'build.py', 'pack.py', 'fetch_vendor.py', 'fetch_sf3000_sdk.py',
                    'INTEGRATION.md', 'status.md', 'tests/test_core.c', 'tests/test_driver.c', 'tests/test_runtime.py',
                    'vendor/libretro.h', 'vendor/jsmn.h', 'vendor/jsmn-LICENSE', 'vendor/sources.json',
                    'tests/hotkey_cases.h','tests/hotkey_transient_cases.h','tests/test_hotkey.c','tests/test_hotkey_runtime.py']
    for source in source_files:
        add('source/' + source, (HERE / source).read_bytes())
    add('source/gamepad-state.h', (HERE / 'gamepad-state.h').read_bytes())
    mpl = HERE / 'vendor/MPL-2.0.txt'
    add('licenses/MPL-2.0.txt', mpl.read_bytes())
    sdk_inputs = args.build_root / 'sf3000-sdk/fetched.json'
    if sdk_inputs.exists():
        add('source/sf3000-sdk-inputs.json', sdk_inputs.read_bytes())
    add('licenses/libretro-header-MIT.txt', (HERE / 'vendor/libretro.h').read_bytes().split(b'#ifndef LIBRETRO_H__')[0])
    add('licenses/jsmn-MIT.txt', (HERE / 'vendor/jsmn-LICENSE').read_bytes())
    add('licenses/gamepad-MIT.txt', (HERE / 'vendor/gamepad-MIT.txt').read_bytes())
    offer = ('Copyright (c) 2026 Jose Pilas. Independent lilretro frontend: MPL-2.0.\n'
             'Complete corresponding source for the independent lilretro frontend,\n'
             'its tests, build scripts, MIT Libretro API header, MIT JSON parser and common input\n'
             'transport header is included in source/. No original PicoArch implementation is used.\n'
             'This archive contains no proprietary game assets or firmware drivers.\n'
             'SDK glibc and SDL libraries are deliberately NOT redistributed: exact corresponding\n'
             'source and build/patch provenance have not been established for those SDK binaries.\n'
             'A repository-level MIT label does not relicense bundled GNU/SDL components.\n'
             'Fallback: install/select an OS-provided matching glibc platform namespace providing\n'
             '/lib/ld.so.1 and the manifest-listed glibc/SDL dependencies, then execute the NEW\n'
             'glibc/lilretro there. This is not a self-contained plug-and-play glibc package.\n'
             'Never dlopen a glibc core into a musl frontend or preload a musl bridge into glibc.\n'
             'See source/INTEGRATION.md for exact CLI and capability limitations.\n')
    add('SOURCE-OFFER.txt', offer.encode())
    for abi, profile in manifest['profiles'].items():
        profile['platform_required'] = sorted(set(profile['platform_required'])-set(profile['bundled_libraries']))
    manifest_data = json.dumps(manifest, indent=2).encode()+b'\n'
    entries['manifest.json'] = (manifest_data, False)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name,(data,executable) in sorted(entries.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026,10,29,0,0,0))
            entry.create_system=3
            entry.external_attr=(0o100755 if executable else 0o100644)<<16
            entry.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(entry,data)
    (args.out.parent/'runtime-manifest.json').write_bytes(manifest_data)
    print(json.dumps(dict(path=str(args.out),bytes=args.out.stat().st_size,
                         sha256=hashlib.sha256(args.out.read_bytes()).hexdigest(),profiles=list(manifest['profiles']),self_contained=False)))

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=HERE/'_build/runtime.zip')
    p.add_argument('--libraries',type=Path)
    p.add_argument('--require-both',action='store_true')
    p.add_argument('--variant',default='')
    p.add_argument('--build-root',type=Path,default=HERE/'_build')
    p.add_argument('--musl-sysroot',type=Path,help='Optional external sysroot for dependency inspection only')
    p.add_argument('--glibc-sysroot',type=Path,help='Optional external sysroot for dependency inspection only')
    pack(p.parse_args())
