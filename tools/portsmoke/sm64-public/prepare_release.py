#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Promote a verified asset-free runtime without changing its game/cache recipe."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile
import zipfile

from review_distribution import source_offer
from split_sources import SOURCE_PREFIX, add_project_snapshot, sha, verified_members


def json_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def safe_member(name):
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or '\\' in name or not name:
        raise ValueError('Unsafe source path: ' + name)
    return path


def copy_member(source, name, target):
    digest = hashlib.sha256()
    size = 0
    with source.open(name) as reader, target.open(name, 'w', force_zip64=True) as writer:
        for block in iter(lambda: reader.read(128 * 1024), b''):
            writer.write(block)
            digest.update(block)
            size += len(block)
    return dict(bytes=size, sha256=digest.hexdigest())


def capture_sdl(sdk, manifest):
    folder = sdk / 'build/sdl2-2.28.5'
    library = folder / 'build/.libs/libSDL2-2.0.so.0.2800.5'
    expected = manifest['files']['runtime/lib/libSDL2-2.0.so.0']['sha256']
    if sha(library) != expected:
        raise ValueError('SDL build no longer matches the qualified packaged library')
    archive = sdk.parent / 'dl/sdl2/SDL2-2.28.5.tar.gz'
    changes, hashes, originals = [], {}, {}
    with tarfile.open(archive, 'r:gz') as original:
        members = {item.name.split('/', 1)[1]: item for item in original
                   if item.isfile() and '/' in item.name}
        for top in ('src', 'include'):
            for source in sorted((folder / top).rglob('*')):
                if not source.is_file() or source.suffix not in ('.c', '.h', '.cpp', '.m', '.mm', '.S'):
                    continue
                name = source.relative_to(folder).as_posix()
                if name in ('include/SDL_config.h', 'include/SDL_revision.h'):
                    continue
                data = source.read_bytes()
                item = members.get(name)
                old = original.extractfile(item).read() if item else b''
                if data == old:
                    continue
                changes.extend(difflib.diff_bytes(difflib.unified_diff,
                    old.splitlines(keepends=True), data.splitlines(keepends=True),
                    fromfile=('a/' + name).encode(), tofile=('b/' + name).encode()))
                hashes[name] = dict(original_sha256=hashlib.sha256(old).hexdigest(),
                                   modified_sha256=hashlib.sha256(data).hexdigest())
                originals[name] = old
        for name in members:
            path = PurePosixPath(name)
            if (path.parts[0] in ('src', 'include') and
                    path.suffix in ('.c', '.h', '.cpp', '.m', '.mm', '.S') and
                    name not in ('include/SDL_config.h', 'include/SDL_revision.h') and
                    not (folder / name).is_file()):
                raise ValueError('Removed SDL source requires explicit handling: ' + name)
    patch = b''.join(changes)
    if patch:
        with tempfile.TemporaryDirectory(prefix='sm64-sdl-source-check-') as scratch:
            root = Path(scratch)
            for name, data in originals.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1'], input=patch,
                           cwd=root, capture_output=True, check=True)
            for name, row in hashes.items():
                if sha(root / name) != row['modified_sha256']:
                    raise ValueError('SDL source patch readback mismatch: ' + name)
    configure = subprocess.check_output([str(folder / 'config.status'), '--config'], text=True)
    prefix = SOURCE_PREFIX + 'build-recipes/sdl2-current/'
    files = {
        prefix + 'SDL2-2.28.5.patch': patch,
        prefix + 'SDL_config.h': (folder / 'include/SDL_config.h').read_bytes(),
        prefix + 'configure-arguments.txt': (configure.strip() + '\n').encode(),
        prefix + 'SOURCE-CHANGES.json': json_bytes(dict(
            upstream_archive=archive.name, upstream_sha256=sha(archive),
            packaged_library_sha256=expected, changed_sources=hashes,
            patch_applied_and_readback_verified=True)),
        prefix + 'README.txt': (
            'Apply SDL2-2.28.5.patch with patch -p1 to the matching upstream archive.\n'
            'configure-arguments.txt records the actual no-GPU MIPS configuration.\n'
            'Use the LilSmoke musl SDK and matching DirectFB/libusb dependencies.\n'
            'SDL_config.h is the generated configuration reference, not a binary.\n'
            'SOURCE-CHANGES.json binds original/modified source and library hashes.\n'
            'No ROM, game asset, kernel or proprietary device firmware is included.\n'
            'SDL retains its original zlib license and source-file copyright notices.\n').encode()
    }
    return files


def write_sources(previous, checkout, sdk, manifest, output):
    replacements = capture_sdl(sdk, manifest)
    for source in (checkout / 'src/input').iterdir():
        if source.is_file():
            replacements[SOURCE_PREFIX + 'lilsmoke-input/' + source.name] = source.read_bytes()
    for name in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py',
                 'sound_data.S', 'build_shell.c', 'bin2c.c', 'public.mk', 'bootstrap.cpp'):
        replacements[SOURCE_PREFIX + 'build-recipes/' + name] = (checkout / 'src/prepare' / name).read_bytes()
    for category in ('lib', 'objects', 'sysroot', 'compiler', 'python'):
        hashes = {name[len('runtime/'):]: row['sha256']
                  for name, row in manifest['files'].items()
                  if name.startswith('runtime/' + category + '/')}
        replacements[SOURCE_PREFIX + category + '-sha256.json'] = json_bytes(hashes)
    dependencies, seen = {}, set()
    with zipfile.ZipFile(previous) as source, zipfile.ZipFile(
            output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as target:
        for item in source.infolist():
            safe_member(item.filename)
            if item.is_dir():
                continue
            if item.filename in seen:
                raise ValueError('Duplicate previous source member')
            seen.add(item.filename)
            if not item.filename.startswith(SOURCE_PREFIX) or item.filename in replacements:
                continue
            dependencies[item.filename] = copy_member(source, item.filename, target)
        for name, data in sorted(replacements.items()):
            target.writestr(name, data)
            dependencies[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        commit = add_project_snapshot(checkout, target)
        target.writestr('SOURCE-MANIFEST.json', json_bytes(dict(schema=2,
            previous_source_zip_sha256=sha(previous), project_source_commit=commit,
            dependency_files=dependencies, rom_included=False, cooked_game_included=False)))
        target.writestr('README.txt',
            'Matching dependencies/build inputs: runtime/corresponding-source/\n'
            'Matching authored project source: project-source/\n'
            'Current SDL source delta: runtime/corresponding-source/build-recipes/sdl2-current/\n'
            'Not needed to install or play. Extract alongside the app for dependency rebuilding.\n'
            'No ROM or locally cooked original-game executable is included.\n')
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('Corresponding-source CRC failed')
    return commit


def release_package(original, output, delivery, version, tag):
    with zipfile.ZipFile(original) as source:
        manifest = json.loads(source.read('runtime/distribution-manifest.json'))
        verified_members(source, manifest)
        metadata = json.loads(source.read('meta.json'))
        if metadata.get('id') != 'org.lilsmoke.sm64':
            raise ValueError('Wrong application identity')
        metadata.update(version=version, release_label=tag)
        materials = json.loads(source.read('runtime/source-material-review.json'))
        materials['external_corresponding_source'] = delivery
        changes = {
            'meta.json': json_bytes(metadata),
            'SOURCE-OFFER.txt': source_offer(delivery).encode(),
            'runtime/source-material-review.json': json_bytes(materials)
        }
        for name, data in changes.items():
            manifest['files'][name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        manifest['corresponding_source_delivery'] = delivery
        with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as target:
            for name in sorted(manifest['files']):
                if name in changes:
                    target.writestr(name, changes[name])
                else:
                    copy_member(source, name, target)
            target.writestr('runtime/distribution-manifest.json', json_bytes(manifest))
        original_recipe = source.read('runtime/recipe.json')
    with zipfile.ZipFile(output) as archive:
        verified_members(archive, manifest)
        if archive.testzip() is not None or archive.read('runtime/recipe.json') != original_recipe:
            raise ValueError('Runtime/cache recipe changed or output CRC failed')
        scripts = [name for name in archive.namelist() if '/' not in name and name.endswith('.sh')]
        if scripts != ['SM64.sh'] or any(name.startswith(SOURCE_PREFIX) for name in archive.namelist()):
            raise ValueError('Incorrect root-script or lean-package layout')
    return dict(files=len(manifest['files']) + 1, operational_payload_unchanged=True,
                recipe_unchanged=True, changed_files=sorted([*changes, 'runtime/distribution-manifest.json']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'previous-sources', 'checkout', 'sdk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--package-sha256', required=True)
    parser.add_argument('--version', default='0.0.2')
    parser.add_argument('--tag', default='Pre-Alpha-0.0.2')
    args = parser.parse_args()
    if sha(args.package) != args.package_sha256:
        raise ValueError('Qualified base package hash mismatch')
    if args.tag != 'Pre-Alpha-' + args.version or '/' in args.version or '\\' in args.version:
        raise ValueError('Invalid release identity')
    with zipfile.ZipFile(args.package) as archive:
        manifest = json.loads(archive.read('runtime/distribution-manifest.json'))
        verified_members(archive, manifest)
        previous = json.loads(archive.read('runtime/source-material-review.json'))['external_corresponding_source']
    if sha(args.previous_sources) != previous['sha256']:
        raise ValueError('Previous matching dependency sources failed hash verification')
    args.output.mkdir(parents=True, exist_ok=False)
    sources = args.output / 'corresponding-source.zip'
    commit = write_sources(args.previous_sources, args.checkout, args.sdk, manifest, sources)
    delivery = dict(asset=sources.name,
        url='https://github.com/josepilas/SM64-LilSmoke/releases/download/' + args.tag + '/' + sources.name,
        sha256=sha(sources), bytes=sources.stat().st_size, project_source_commit=commit)
    package = args.output / ('SM64-LilSmoke-' + args.tag + '.lilpck')
    result = release_package(args.package, package, delivery, args.version, args.tag)
    result.update(tag=args.tag, version=args.version, source_commit=commit,
        base_package_sha256=args.package_sha256, rom_included=False,
        cooked_game_included=False, renderer_binary_included=False,
        corresponding_source=delivery, package=package.name, package_bytes=package.stat().st_size,
        package_sha256=sha(package), uploaded=False)
    for artifact in (package, sources):
        artifact.with_suffix(artifact.suffix + '.sha256').write_text(sha(artifact) + '  ' + artifact.name + '\n')
    (args.output / 'release-report.json').write_bytes(json_bytes(result))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
