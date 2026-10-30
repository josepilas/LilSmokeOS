#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
"""Offline, allowlisted standalone publication. Never writes the input site."""
import argparse
import hashlib
import io
import json
import posixpath
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
SOURCE_ZIP = 'LilSmoke-Port-Converter-0.2.0-source.zip'
DIST_ZIP = 'LilSmoke-Port-Converter-0.2.0-dist.zip'
WEB = ('index.html', 'app.js', 'style.css', 'worker.js', 'icons.js')
RUNTIME = ('lilretro.c', 'gamepad-state.h', 'build.py', 'pack.py', 'fetch_vendor.py',
           'fetch_sf3000_sdk.py', 'INTEGRATION.md', 'status.md')
RUNTIME_TESTS = ('test_core.c', 'test_driver.c', 'test_runtime.py', 'test_hotkey.c',
                 'test_hotkey_runtime.py', 'hotkey_cases.h', 'hotkey_transient_cases.h')
RUNTIME_VENDOR = ('libretro.h', 'jsmn.h', 'jsmn-LICENSE', 'sources.json', 'MPL-2.0.txt', 'gamepad-MIT.txt')
FILES = ('README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'package.json', 'build.py',
         'source_release.py', 'publication/.gitignore', 'publication/DIST-README.md',
         'src/converter.c', 'src/main.c', 'src/zip_modes.c', 'vendor/manifest.json',
         'tests/test_core.py', 'tests/security_qa.py', 'tests/native_archives.py',
         'tests/probe_real.py', 'tests/browser.mjs', 'tests/test_publication.py',
         'tests/serve.py', 'ui-tests/smoke.cjs', 'ui-tests/fixture.ps1', 'ui-tests/inspect-package.ps1')
LICENSES = ('MPL-2.0.txt', 'miniz.txt', 'cjson.txt', 'emscripten.txt', 'wasm-musl.txt', 'compiler-rt.txt')
FIXED_TIME = (2026, 10, 29, 0, 0, 0)
FORBIDDEN_PARTS = {'.git', '.openai', 'node_modules', '__pycache__', 'captures', 'artifacts'}
PRIVATE = re.compile(rb'(?i)(?:[a-z]:[\\/]users[\\/]|/mnt/[a-z]/|/ro[o]t/|/ho[m]e/[^/\s]+/|'
                     rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|'
                     rb'sk-(?:proj-)?[A-Za-z0-9_-]{30,}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|'
                     rb'AIza[0-9A-Za-z_-]{35}|AKIA[0-9A-Z]{16})')


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + '\n').encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe archive path: ' + name)
    if any(part.lower() in FORBIDDEN_PARTS or part.lower().startswith('.env') for part in path.parts):
        raise ValueError('Forbidden publication path: ' + name)


def audit(entries, prefix='', depth=0):
    if depth > 3:
        raise ValueError('Unexpected deeply nested archive')
    folded = set()
    checked = 0
    for name, data in sorted(entries.items()):
        safe_name(name)
        if name.casefold() in folded:
            raise ValueError('Case-insensitive duplicate: ' + name)
        folded.add(name.casefold())
        if PRIVATE.search(data):
            raise ValueError('Private path or credential signature: ' + prefix + name)
        if name.endswith(('.lilpck', '.png', '.jpg', '.mp4', '.pyc', '.iso', '.img', '.7z')):
            raise ValueError('Unexpected private/binary asset: ' + name)
        checked += 1
        if name.endswith('.zip'):
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                if archive.testzip() is not None:
                    raise ValueError('Corrupt nested archive: ' + name)
                if len(archive.namelist()) != len(set(archive.namelist())):
                    raise ValueError('Duplicate nested ZIP entry')
                checked += audit({n: archive.read(n) for n in archive.namelist()}, prefix + name + '!', depth + 1)
    return checked


def read(relative, root=HERE):
    path = root / relative
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Missing or unsafe input: ' + relative)
    return path.read_bytes()


def runtime_sources():
    names = list(RUNTIME) + ['tests/' + n for n in RUNTIME_TESTS] + ['vendor/' + n for n in RUNTIME_VENDOR]
    return {name: read('runtime/' + name) for name in names}


def inventory(entries):
    return {name: {'bytes': len(data), 'sha256': digest(data)} for name, data in sorted(entries.items())}


def zip_bytes(entries, executable=()):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            safe_name(name)
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.create_system = 3
            info.external_attr = (0o100755 if name in executable else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compresslevel=9)
    return stream.getvalue()


def load_image(data):
    """Compare ELF32 MIPS load segments, excluding section-table header fields."""
    if data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 8:
        raise ValueError('Expected ELF32 little-endian MIPS')
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    result = []
    for i in range(count):
        kind, start, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', data, offset + size * i)
        if kind == 1:
            content = bytearray(data[start:start + filesz])
            if start == 0:
                content[32:36] = bytes(4)
                content[46:52] = bytes(6)
            result.append(((start, vaddr, paddr, filesz, memsz, flags, align), bytes(content)))
    if not result:
        raise ValueError('No loadable ELF segments')
    return result


def portable_build(build, abi):
    command = build['command']
    source = next(s for s in command if s.endswith('/runtime/lilretro.c'))
    project = source[:-len('/runtime/lilretro.c')]
    compiler = build['compiler']
    os_root = project.rsplit('/tools/treefrog-converter', 1)[0]
    if abi == 'musl':
        sdk = compiler.split('/host/bin/')[0]
    else:
        sysroot = next(s.split('=', 1)[1] for s in command if s.startswith('--sysroot='))
        sdk = posixpath.commonpath([compiler, sysroot])
    replacements = sorted(((project, '${PROJECT_ROOT}'), (sdk, '${SDK_ROOT}'),
        (os_root + '/package/lilsmoke-ui/src/src', '${PROJECT_ROOT}/runtime')), key=lambda item: -len(item[0]))
    def clean(value):
        if isinstance(value, str):
            for old, new in replacements:
                value = value.replace(old, new)
            return value
        if isinstance(value, list):
            return [clean(item) for item in value]
        if isinstance(value, dict):
            return {clean(key): clean(item) for key, item in value.items()}
        return value
    portable = clean(build)
    portable['sources'] = {'runtime/' + (k.split('/runtime/', 1)[-1] if '/runtime/' in k else Path(k).name): v
                           for k, v in build['sources'].items()}
    portable['publication_note'] = 'Original build-input and frozen-binary hashes; paths replaced with portable placeholders. See publication-provenance.json for distributed ELF identity.'
    return portable


def public_runtime(original, elf_tool):
    with zipfile.ZipFile(io.BytesIO(original)) as archive:
        if archive.testzip() is not None:
            raise ValueError('Corrupt input runtime')
        old = {name: archive.read(name) for name in archive.namelist()}
    manifest = json.loads(old['manifest.json'])
    if set(manifest['profiles']) != {'musl', 'glibc'}:
        raise ValueError('Both independent frontends are required')
    if any(p['bundled_libraries'] for p in manifest['profiles'].values()):
        raise ValueError('External library redistribution needs a separate license review')
    entries = {'source/' + n: d for n, d in runtime_sources().items()}
    entries['SOURCE-OFFER.txt'] = old['SOURCE-OFFER.txt']
    entries['licenses/MPL-2.0.txt'] = read('LICENSE')
    entries['licenses/jsmn-MIT.txt'] = read('runtime/vendor/jsmn-LICENSE')
    entries['licenses/libretro-header-MIT.txt'] = old['licenses/libretro-header-MIT.txt']
    entries['licenses/gamepad-MIT.txt'] = read('runtime/vendor/gamepad-MIT.txt')
    records = {}
    with tempfile.TemporaryDirectory(prefix='public-runtime-') as directory:
        for abi in ('musl', 'glibc'):
            name = abi + '/lilretro'
            before = old[name]
            path = Path(directory) / abi
            path.write_bytes(before)
            subprocess.run([elf_tool, '--strip-debug', str(path)], check=True)
            after = path.read_bytes()
            if PRIVATE.search(after):
                subprocess.run([elf_tool, '--strip-unneeded', str(path)], check=True)
                after = path.read_bytes()
            if load_image(before) != load_image(after):
                raise ValueError('ELF loadable bytes changed: ' + abi)
            entries[name] = after
            build = json.loads(old['source/build-' + abi + '.json'])
            portable = portable_build(build, abi)
            entries['source/build-' + abi + '.json'] = json_bytes(portable)
            records[abi] = dict(original_sha256=digest(before), published_sha256=digest(after),
                loadable_segments_identical_except_section_table_header=True,
                original_build_record_sha256=digest(old['source/build-' + abi + '.json']),
                original_build_input_sha256=portable['sources'],
                transformation='Nonloadable debug/file symbols removed; section-table header fields may change')
    entries['source/publication-provenance.json'] = json_bytes(dict(
        original_runtime_sha256=digest(original), binaries=records,
        source_policy='Current complete source and portable build tools; frozen input hashes retained above'))
    manifest['files'] = {name: dict(sha256=digest(data), bytes=len(data),
        **{k: v for k, v in manifest.get('files', {}).get(name, {}).items() if k not in ('sha256', 'bytes')})
        for name, data in sorted(entries.items())}
    manifest['publication'] = dict(debug_paths_removed=True, executable_loadable_bytes_preserved=True,
                                    original_runtime_sha256=digest(original))
    entries['manifest.json'] = json_bytes(manifest)
    audit(entries)
    return zip_bytes(entries, ('musl/lilretro', 'glibc/lilretro')), records


def source_entries(web):
    entries = {name: read(name) for name in FILES}
    entries['.gitignore'] = read('publication/.gitignore')
    entries.update({'runtime/' + n: d for n, d in runtime_sources().items()})
    entries.update({'licenses/' + n: read('licenses/' + n) for n in LICENSES})
    entries.update({'site/dist/' + n: read(n, web) for n in WEB})
    for directory, names in (
        ('miniz', ('miniz.c', 'miniz.h', 'miniz_common.h', 'miniz_tdef.c', 'miniz_tdef.h',
                   'miniz_tinfl.c', 'miniz_tinfl.h', 'miniz_zip.c', 'miniz_zip.h', 'miniz_export.h', 'LICENSE')),
        ('cjson', ('cJSON.c', 'cJSON.h', 'LICENSE'))):
        for name in names:
            relative = 'vendor/' + directory + '/' + name
            entries[relative] = read(relative)
    entries['SHA256SUMS.json'] = json_bytes(inventory(entries))
    return entries


def write_tree(root, entries):
    if root.exists() and any(root.iterdir()):
        raise ValueError('Output must be empty (no implicit deletion): ' + str(root))
    root.mkdir(parents=True, exist_ok=True)
    for name, data in sorted(entries.items()):
        safe_name(name)
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=HERE / '_build/github-publication')
    parser.add_argument('--web', type=Path, default=HERE / 'site/dist')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--elf-tool', default='llvm-strip-15')
    args = parser.parse_args()
    output = args.out.resolve()
    if not output.is_relative_to((HERE / '_build').resolve()):
        parser.error('Publication output must stay under this converter _build')
    if output.exists() and any(output.iterdir()):
        parser.error('Output exists and is not empty; use a fresh --out')
    web = args.web.resolve(strict=True)
    protected = {p: digest(p.read_bytes()) for p in web.rglob('*') if p.is_file()}
    sources = source_entries(web)
    runtime_path = args.runtime or web / 'runtime/runtime.zip'
    runtime, records = public_runtime(runtime_path.read_bytes(), args.elf_tool)
    source_zip = zip_bytes(sources)
    dist = {name: read(name, web) for name in (*WEB, 'converter.js', 'converter.wasm')}
    dist.update({'licenses/' + n: read('licenses/' + n) for n in LICENSES})
    with zipfile.ZipFile(io.BytesIO(runtime)) as archive:
        dist.update({n: archive.read(n) for n in archive.namelist() if n.startswith('licenses/')})
    dist.update({'LICENSE': read('LICENSE'), 'README.md': read('publication/DIST-README.md'),
                 'THIRD-PARTY-NOTICES.md': read('THIRD-PARTY-NOTICES.md'), '.nojekyll': b'',
                 'runtime/runtime.zip': runtime, 'source/' + SOURCE_ZIP: source_zip})
    dist['source/manifest.json'] = json_bytes(dict(path=SOURCE_ZIP, bytes=len(source_zip),
        files=len(sources), sha256=digest(source_zip), user_data_included=False))
    dist['SHA256SUMS.json'] = json_bytes(inventory(dist))
    checked = audit(sources) + audit(dist)
    write_tree(output / 'source', sources)
    write_tree(output / 'dist', dist)
    (output / SOURCE_ZIP).write_bytes(source_zip)
    dist_zip = zip_bytes(dist)
    (output / DIST_ZIP).write_bytes(dist_zip)
    report = dict(schema=1, branches=['source', 'dist'], project_prefix='/LilSmoke-Port-Converter/',
        source=inventory(sources), dist=inventory(dist), archives=inventory({SOURCE_ZIP: source_zip, DIST_ZIP: dist_zip}),
        audit=dict(passed=True, files_checked_including_nested=checked, allowlisted_inputs=True,
                   private_qa_excluded=['tests/full_vm.py', 'tests/vm_pad.c', '_build', 'ui-tests/artifacts'],
                   sdk_firmware_user_archives_included=False, credential_signatures_found=False),
        runtime_publication=records, input_site_unchanged=all(digest(p.read_bytes()) == h for p, h in protected.items()))
    if not report['input_site_unchanged']:
        raise ValueError('Input site changed during publication')
    (output / 'publication.json').write_bytes(json_bytes(report))
    print(json.dumps(dict(output=str(output), files_source=len(sources), files_dist=len(dist),
                          archives=report['archives'], audit_passed=True)))


if __name__ == '__main__':
    main()
