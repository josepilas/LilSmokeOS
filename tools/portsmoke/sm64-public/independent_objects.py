#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fail-closed installation of explicitly audited, ROM-independent objects.

The caller must pin the manifest digest outside the bundle. This is not a
signature scheme, licence clearance, or a replacement for GNU Make.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile


SCHEMA = 'lilsmoke-independent-objects-v1'
LIMIT = 4 * 1024 * 1024
TOOL_LIMIT = 64 * 1024 * 1024
BUILD = 'build/original-mips/'
# Do not broaden this list on the strength of the separate base CC0 notice.
# These two adaptations are covered by the archive's lilsmoke/LICENSE.
ELIGIBLE = {
    'lilsmoke/m8-audio.c': ('lilsmoke-audio.o', ['-std=gnu11']),
    'lilsmoke/m8_pocketsnes_audio.c': ('lilsmoke-pcm.o', ['-std=gnu11', '-Ililsmoke']),
}
SOURCE_INPUTS = set(ELIGIBLE) | {
    'lilsmoke/m8-audio.h', 'lilsmoke/m8_pocketsnes_audio.h', 'lilsmoke/LICENSE',
    'lilsmoke/public.mk',
}


def safe_name(name):
    if (not isinstance(name, str) or len(name) > 512 or
            not re.fullmatch(r'[A-Za-z0-9_.+/-]+', name) or
            any(part in ('', '.', '..') for part in name.split('/'))):
        raise ValueError('Unsafe relative path')
    return name


def safe_path(root, name, create_parents=False):
    root = Path(root).absolute()
    # Reject a symlink in any ancestor, not only the final component.
    for item in reversed((root, *root.parents)):
        if stat.S_ISLNK(item.lstat().st_mode):
            raise ValueError('Symlink in root path')
    if not root.is_dir():
        raise ValueError('Root is not a directory')
    current = root
    parts = safe_name(name).split('/')
    for part in parts[:-1]:
        current = current / part
        if create_parents and not current.exists():
            current.mkdir(mode=0o700)
        mode = current.lstat().st_mode
        if not stat.S_ISDIR(mode) or stat.S_ISLNK(mode):
            raise ValueError('Unsafe parent directory')
    target = current / parts[-1]
    if target.is_symlink():
        raise ValueError('Symlink file')
    return target


def read_regular(path, maximum=LIMIT, allow_empty=False):
    path = Path(path)
    before = path.lstat()
    if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
            before.st_size < (0 if allow_empty else 1) or before.st_size > maximum):
        raise ValueError('Not a bounded, unshared regular file: ' + str(path))
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(fd, 'rb') as handle:
        opened = os.fstat(handle.fileno())
        if (before.st_dev, before.st_ino, before.st_size) != (
                opened.st_dev, opened.st_ino, opened.st_size):
            raise ValueError('File changed while opening')
        data = handle.read(maximum + 1)
        after = os.fstat(handle.fileno())
    if len(data) != before.st_size or (opened.st_mtime_ns, opened.st_size) != (
            after.st_mtime_ns, after.st_size):
        raise ValueError('File changed while reading')
    return data


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fingerprint(path, maximum=LIMIT, allow_empty=False):
    path = Path(path)
    before = path.lstat()
    if (not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or
            not (0 if allow_empty else 1) <= before.st_size <= maximum):
        raise ValueError('Not a bounded, unshared regular file: ' + str(path))
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    digest, size = hashlib.sha256(), 0
    with os.fdopen(fd, 'rb') as handle:
        opened = os.fstat(handle.fileno())
        if (before.st_dev, before.st_ino, before.st_size) != (
                opened.st_dev, opened.st_ino, opened.st_size):
            raise ValueError('File changed while opening')
        while True:
            block = handle.read(128 * 1024)
            if not block:
                break
            size += len(block)
            if size > maximum:
                raise ValueError('File grew beyond budget')
            digest.update(block)
        after = os.fstat(handle.fileno())
    if size != before.st_size or (opened.st_mtime_ns, opened.st_size) != (after.st_mtime_ns, after.st_size):
        raise ValueError('File changed while hashing')
    return dict(bytes=size, sha256=digest.hexdigest())


def record(path, name, maximum=LIMIT, allow_empty=False):
    return dict(path=safe_name(name), **fingerprint(path, maximum, allow_empty))


def checked_record(root, item, maximum=LIMIT, load=True, allow_empty=False):
    if not isinstance(item, dict) or set(item) != {'path', 'bytes', 'sha256'}:
        raise ValueError('Invalid file record')
    if (type(item['bytes']) is not int or not (0 if allow_empty else 1) <= item['bytes'] <= maximum or
            not isinstance(item['sha256'], str) or
            not re.fullmatch('[0-9a-f]{64}', item['sha256'])):
        raise ValueError('Invalid size or digest')
    path = safe_path(root, item['path'])
    data = read_regular(path, maximum, allow_empty) if load else None
    actual = dict(bytes=len(data), sha256=sha(data)) if load else fingerprint(path, maximum, allow_empty)
    if actual != {key: item[key] for key in ('bytes', 'sha256')}:
        raise ValueError('File checksum or size mismatch: ' + item['path'])
    return data


def verify_object(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Not little-endian ELF32')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    flags = struct.unpack_from('<I', data, 36)[0]
    if (kind, machine, version) != (1, 8, 1) or flags & 0xF000F000 != 0x70001000:
        raise ValueError('Not MIPS32r2 o32 ET_REL')
    offset = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    if stride != 40 or not count or offset < 52 or offset + stride * count > len(data):
        raise ValueError('Invalid section table')
    abis = []
    for index in range(count):
        section = struct.unpack_from('<10I', data, offset + stride * index)
        if section[1] == 0x7000002A:  # SHT_MIPS_ABIFLAGS
            begin, size = section[4:6]
            if size != 24 or begin + size > len(data):
                raise ValueError('Invalid ABI flags')
            abis.append(struct.unpack_from('<H6B4I', data, begin))
    # version, ISA level/revision, GPR/CPR1 sizes, CPR2 size, double hard-float
    if len(abis) != 1 or abis[0][:7] != (0, 32, 2, 1, 1, 0, 1):
        raise ValueError('Not MIPS32r2 fp32 hard-float ABI')


def normalized_flags(flags, runtime):
    if not isinstance(flags, (list, tuple)) or not all(isinstance(x, str) for x in flags):
        raise ValueError('Flags must be an argv list')
    return [item.replace(str(Path(runtime).absolute()), '@runtime') for item in flags]


def make_escape(name):
    return str(name).replace('$', '$$').replace('#', '\\#').replace(' ', '\\ ')


def atomic_write(path, data, epoch):
    fd, temporary = tempfile.mkstemp(prefix='.independent-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.utime(temporary, (epoch, epoch))
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def install_objects(bundle, work, runtime, *, expected_manifest_sha256,
                    source_sha256, flags, epoch=0, cancel=lambda: False):
    """Validate everything first; changed prerequisites skip only that object.

    Return installed/invalidated lists, a skipped reason map, and makefiles to
    explicitly add to GNU Make's -f inputs (the upstream DEP_FILES omits these
    late-added adaptation targets). Malformed
    bundles, unsafe paths and object corruption raise ValueError with no writes.
    Call before build planning, in the caller's exclusively owned scratch tree.
    Cancellation raises InterruptedError; the ordinary Make rules remain valid.
    """
    bundle, work, runtime = (Path(path).absolute() for path in (bundle, work, runtime))
    manifest_data = read_regular(safe_path(bundle, 'manifest.json'), 2 * LIMIT)
    if sha(manifest_data) != expected_manifest_sha256:
        raise ValueError('Untrusted manifest digest')
    manifest = json.loads(manifest_data)
    if (manifest.get('schema') != SCHEMA or manifest.get('source_sha256') != source_sha256 or
            manifest.get('flags') != normalized_flags(flags, runtime) or
            manifest.get('audit', {}).get('distribution_scope') != 'MPL-2.0 LilSmoke adaptations only'):
        raise ValueError('Object bundle build contract mismatch')
    entries = manifest.get('objects')
    tools = manifest.get('toolchain')
    if not isinstance(entries, list) or len(entries) > len(ELIGIBLE) or not isinstance(tools, list) or not tools:
        raise ValueError('Invalid manifest lists')
    seen_tools = set()
    for item in tools:
        if cancel():
            raise InterruptedError('Independent object installation cancelled')
        name = safe_name(item['path'])
        if not name.startswith('compiler/') or name in seen_tools:
            raise ValueError('Unexpected toolchain input')
        seen_tools.add(name)
        checked_record(runtime, item, TOOL_LIMIT, load=False)
    prepared, skipped, seen, invalidations = [], {}, set(), []
    for entry in entries:
        if cancel():
            raise InterruptedError('Independent object installation cancelled')
        source = entry.get('source')
        if source not in ELIGIBLE or source in seen:
            raise ValueError('Unapproved or duplicate source')
        seen.add(source)
        leaf, extra = ELIGIBLE[source]
        target = BUILD + leaf
        if entry.get('target') != target or entry.get('extra_flags') != extra:
            raise ValueError('Unapproved target or flags')
        obj = entry['object']
        if obj.get('path') != 'objects/' + leaf:
            raise ValueError('Unexpected object path')
        data = checked_record(bundle, obj)
        verify_object(data)
        prerequisites = entry.get('prerequisites')
        if not isinstance(prerequisites, list) or not 1 <= len(prerequisites) <= 2048:
            raise ValueError('Invalid prerequisite list')
        dependencies, latest, changed, names = [], int(epoch), [], set()
        for item in prerequisites:
            if cancel():
                raise InterruptedError('Independent object installation cancelled')
            scope = item.get('scope')
            name = safe_name(item.get('path'))
            key = (scope, name)
            if key in names or scope not in ('source', 'runtime'):
                raise ValueError('Duplicate or invalid prerequisite scope')
            names.add(key)
            if scope == 'source' and name not in SOURCE_INPUTS:
                raise ValueError('Unapproved source prerequisite')
            if scope == 'runtime' and not name.startswith(('sysroot/usr/include/', 'compiler/usr/lib/gcc/mipsel-linux-gnu/12/include/')):
                raise ValueError('Unapproved runtime prerequisite')
            root = work if scope == 'source' else runtime
            try:
                path = safe_path(root, name)
                checked_record(root, {k: v for k, v in item.items() if k != 'scope'}, allow_empty=True)
            except FileNotFoundError:
                changed.append(name)
            except ValueError as error:
                # Unsafe files must fail closed, not be silently treated as stale.
                if 'checksum or size mismatch' not in str(error):
                    raise
                changed.append(name)
            else:
                latest = max(latest, int(path.stat().st_mtime) + 2)
            dependencies.append(name if scope == 'source' else str(Path(runtime).absolute() / name))
        if not {('source', source), ('source', 'lilsmoke/LICENSE'), ('source', 'lilsmoke/public.mk')} <= names:
            raise ValueError('Missing source, licence prerequisite or compile recipe')
        # Check destinations even for stale entries, without creating parents yet.
        parent = Path(work) / 'build/original-mips'
        if parent.exists():
            for suffix in ('.o', '.d'):
                path = safe_path(work, target[:-2] + suffix)
                if path.exists():
                    read_regular(path)
        if changed:
            skipped[target] = 'changed prerequisites: ' + ', '.join(changed)
            if parent.exists():
                invalidations.extend(path for path in (
                    safe_path(work, target), safe_path(work, target[:-2] + '.d')) if path.exists())
            continue
        rule = make_escape(target) + ': ' + ' '.join(make_escape(x) for x in dependencies) + '\n'
        rule += ''.join(make_escape(x) + ':\n' for x in dependencies)
        prepared.append((target, data, rule.encode('utf-8'), latest))
    installed, invalidated = [], []
    for path in invalidations:
        if cancel():
            raise InterruptedError('Independent object installation cancelled')
        path.unlink()
        invalidated.append(path.relative_to(work).as_posix())
    for target, data, rule, stamp in prepared:
        if cancel():
            raise InterruptedError('Independent object installation cancelled')
        destination = safe_path(work, target, create_parents=True)
        dependency = safe_path(work, target[:-2] + '.d')
        # Commit dependency first and object last, so interruption cannot bless
        # a new object lacking prerequisite tracking.
        atomic_write(dependency, rule, stamp)
        atomic_write(destination, data, stamp)
        installed.append(target)
    return dict(installed=installed, skipped=skipped, invalidated=invalidated,
                makefiles=[target[:-2] + '.d' for target in installed])
