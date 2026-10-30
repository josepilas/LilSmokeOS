#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Private, recipe-bound extraction checkpoints; never distribution inputs."""
import json
import os
import shutil
from pathlib import Path, PurePosixPath
import stat
import time

from cook import Cancelled, US_SHA1, atomic_json, digest_file, directory, regular

MARKER = 'assets-ready.json'
MAX_FILES = 12000
MAX_BYTES = 192 * 1024 * 1024


def source_files(work, cancel=lambda: False):
    directory(work)
    total = 0
    count = 0
    for root, dirs, files in os.walk(work, followlinks=False):
        for name in dirs + files:
            path = Path(root) / name
            info = path.lstat()
            if not (stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode)):
                raise ValueError('Saved preparation contains a link or special file')
            count += 1
            if cancel():
                raise Cancelled('Preparation cancelled; original ROM kept')
            if count > MAX_FILES * 3:
                raise ValueError('Saved preparation exceeds the device budget')
            relative = path.relative_to(work)
            if stat.S_ISREG(info.st_mode):
                regular(path, 32 * 1024 * 1024)
            if relative.parts[0] == 'build' or not stat.S_ISREG(info.st_mode):
                continue
            total += info.st_size
            if total > MAX_BYTES or count > MAX_FILES * 3:
                raise ValueError('Saved preparation exceeds the device budget')
            yield relative.as_posix(), path, info.st_size


def checkpoint(stage, recipe, extracted, progress, cancel=lambda: False):
    files = {}
    last = 0
    for name, path, size in source_files(stage / 'source', cancel):
        files[name] = dict(bytes=size, sha256=digest_file(path))
        if len(files) > MAX_FILES:
            raise ValueError('Saved preparation has too many source files')
        if time.monotonic() - last > 1:
            progress('Saving extracted assets', 50, extracted)
            last = time.monotonic()
    atomic_json(stage / MARKER, dict(schema=1, recipe=recipe, rom_sha1=US_SHA1,
        extracted_bytes=extracted, files=files))


def validate(stage, recipe, progress, cancel=lambda: False):
    regular(stage / MARKER, 2 * 1024 * 1024)
    value = json.loads((stage / MARKER).read_text())
    extracted = value.get('extracted_bytes')
    if (value.get('schema') != 1 or value.get('recipe') != recipe or
            value.get('rom_sha1') != US_SHA1 or type(extracted) is not int or
            not 0 < extracted <= 128 * 1024 * 1024):
        raise ValueError('Saved preparation belongs to a different build')
    files = value.get('files')
    if not isinstance(files, dict) or not 0 < len(files) <= MAX_FILES:
        raise ValueError('Invalid saved preparation index')
    for name, row in files.items():
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or '..' in path.parts or '\\' in name or
                path.parts[0] == 'build' or not isinstance(row, dict)):
            raise ValueError('Unsafe saved preparation path')
    seen = set()
    last = 0
    progress('Checking saved work', 50, extracted)
    for name, path, size in source_files(stage / 'source', cancel):
        row = files.get(name)
        if not row or row.get('bytes') != size or row.get('sha256') != digest_file(path):
            raise ValueError('Saved preparation changed; original ROM kept')
        seen.add(name)
        if time.monotonic() - last > 1:
            progress('Checking saved work', 50, extracted)
            last = time.monotonic()
    if seen != files.keys():
        raise ValueError('Saved preparation is incomplete; original ROM kept')
    return extracted


def active_stage(root, recipe, progress, cancel=lambda: False):
    active = root / 'active.json'
    if not active.exists():
        return None
    regular(active, 4096)
    value = json.loads(active.read_text())
    name = value.get('generation')
    if (value.get('schema') != 1 or value.get('recipe') != recipe or
            value.get('rom_sha1') != US_SHA1):
        return None
    if (not isinstance(name, str) or not name.startswith('working-') or
            len(name) > 80 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-_' for c in name)):
        raise ValueError('Unsafe saved preparation generation')
    stage = root / name
    directory(stage)
    if not (stage / MARKER).exists():
        return None
    validate(stage, recipe, progress, cancel)
    return stage


def inflight(stage, target):
    relative = PurePosixPath(target)
    if (relative.is_absolute() or '..' in relative.parts or '\\' in target or
            relative.parts[:2] != ('build', 'original-mips') or len(relative.parts) < 3):
        raise ValueError('Unsafe generated build target')
    atomic_json(stage / 'inflight.json', dict(schema=1, target=relative.as_posix()))


def inflight_restart(stage):
    # Unknown/multi-output producer: preserve extracted inputs, but rebuild
    # generated outputs if interrupted rather than reusing a partial artifact.
    atomic_json(stage / 'inflight.json', dict(schema=1, restart_build=True))


def clean_inflight(stage):
    marker = stage / 'inflight.json'
    if not marker.exists():
        return
    regular(marker, 4096)
    value = json.loads(marker.read_text())
    if value.get('schema') == 1 and value.get('restart_build') is True:
        output = stage / 'source/build'
        if output.exists() or output.is_symlink():
            directory(output)
            shutil.rmtree(output)
        marker.unlink()
        return
    target = value.get('target')
    if value.get('schema') != 1 or not isinstance(target, str):
        raise ValueError('Invalid interrupted target record')
    relative = PurePosixPath(target)
    if (relative.is_absolute() or '..' in relative.parts or '\\' in target or
            relative.parts[:2] != ('build', 'original-mips') or len(relative.parts) < 3):
        raise ValueError('Unsafe interrupted build target')
    path = stage / 'source'
    for part in relative.parts[:-1]:
        path /= part
        directory(path)
    output = path / relative.name
    if output.exists() or output.is_symlink():
        regular(output)
        output.unlink()
    marker.unlink()
