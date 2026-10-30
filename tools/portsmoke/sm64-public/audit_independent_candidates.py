#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Dependency-only generic-engine feasibility audit. Never produces objects.

Reuse one existing isolated compiler jail, avoiding another toolchain copy.
Source-only feasibility and permission to distribute are separate findings.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tarfile

from independent_objects import fingerprint, normalized_flags, read_regular, record, safe_name, sha
from prebuild_independent import SOURCE_SHA256, production_flags, run_compile, verify_runtime_inputs
from prebuild_independent import parse_dependencies


CANDIDATES = tuple('src/' + name + '.c' for name in (
    'engine/behavior_script', 'engine/geo_layout', 'engine/graph_node',
    'engine/graph_node_manager', 'engine/level_script', 'engine/math_util',
    'engine/surface_collision', 'engine/surface_load',
    'audio/effects', 'audio/external', 'audio/heap', 'audio/load',
    'audio/playback', 'audio/seqplayer', 'audio/synthesis',
    'game/camera', 'game/interaction', 'game/level_update',
    'game/macro_special_objects', 'game/mario', 'game/mario_actions_airborne',
    'game/mario_actions_automatic', 'game/mario_actions_cutscene',
    'game/mario_actions_moving', 'game/mario_actions_object',
    'game/mario_actions_stationary', 'game/mario_actions_submerged',
    'game/mario_misc', 'game/mario_step', 'game/memory',
    'game/obj_behaviors', 'game/obj_behaviors_2', 'game/object_collision',
    'game/object_helpers', 'game/object_list_processor',
    'game/platform_displacement', 'game/profiler', 'game/save_file',
    'game/sound_init', 'game/spawn_object', 'game/spawn_sound', 'game/thread6',
    'pc/configfile', 'pc/fsutils', 'pc/mixer',
    'pc/controller/controller_entry_point', 'pc/controller/controller_recorded_tas',
    'pc/controller/controller_keyboard',
))


def classify_dependencies(dependencies, available):
    missing = sorted(name for name in dependencies
                     if not name.startswith('/runtime/') and name not in available)
    graphics = sorted(name for name in dependencies if name.startswith('src/pc/gfx/'))
    restricted = sorted(name for name in dependencies if name.startswith('include/PR/'))
    data = sorted(name for name in dependencies
                  if name.startswith(('build/', 'assets/', 'textures/', 'bin/', 'sound/', 'text/'))
                  or name.endswith(('.inc.c', '.png', '.bin', '.o')))
    return dict(source_only_closure=not (missing or graphics or data),
                missing_or_generated=missing, graphics_boundary=graphics,
                proprietary_header_boundary=restricted, data_boundary=data,
                distribution_approved=False,
                permission_block='No blanket pinned-fork licence grant; PR notices unresolved')


def audit(runtime, jail, out):
    runtime, jail, out = (path.absolute() for path in (runtime, jail, out))
    if (out.parent != Path('/root') or not out.name.startswith('sm64-independent-') or
            out.exists() or out.is_symlink() or jail.resolve() != jail or
            jail.parent.parent != Path('/root') or not jail.parent.name.startswith('sm64-independent-')):
        raise ValueError('Use fresh /root/sm64-independent-* output and an existing sidecar jail')
    verified = verify_runtime_inputs(runtime)
    for base in ('compiler', 'sysroot/usr/include'):
        for source in (runtime / base).rglob('*'):
            if source.is_file():
                copy = jail / 'runtime' / source.relative_to(runtime)
                if fingerprint(source, 64 * 1024 * 1024, True) != fingerprint(copy, 64 * 1024 * 1024, True):
                    raise ValueError('Reused compiler jail changed')
    directory = jail / 'audit-source'
    directory.mkdir()
    out.mkdir()
    (out / 'logs').mkdir()
    available = set()
    if sha(read_regular(runtime / 'source.tar.gz', 32 * 1024 * 1024)) != SOURCE_SHA256:
        raise ValueError('Source archive changed')
    # Deliberately omit every gfx file, data include and generated input.
    # No Makefile, extractor, ROM, asset tool, or compiled game object is used.
    with tarfile.open(runtime / 'source.tar.gz', 'r:gz') as archive:
        for member in archive:
            if not member.isfile():
                continue
            name = safe_name(member.name)
            if name.startswith('src/pc/gfx/') or not (name.endswith('.h') or name in CANDIDATES):
                continue
            data = archive.extractfile(member).read()
            if len(data) != member.size:
                raise ValueError('Truncated pinned source')
            target = directory / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o444)
            available.add(name)
    dependencies_dir = directory / 'dependencies'
    dependencies_dir.mkdir(mode=0o777)
    dependencies_dir.chmod(0o777)
    flags = [value.replace('@runtime', '/runtime')
             for value in normalized_flags(production_flags(runtime), runtime)]
    units = []
    for index, source in enumerate(CANDIDATES):
        dependency_file = 'dependencies/' + str(index) + '.d'
        argv = [*flags, '-M', '-MG', '-MT', 'independent-audit', '-MF', dependency_file, source]
        log = out / 'logs' / (str(index) + '.log')
        try:
            run_compile(jail, argv, log, working_directory='/audit-source')
        except subprocess.CalledProcessError as error:
            units.append(dict(source=source, source_only_closure=False,
                              distribution_approved=False, preprocessing_exit=error.returncode))
            continue
        _, dependencies = parse_dependencies(read_regular(directory / dependency_file, 256 * 1024).decode())
        unit = dict(source=source, **classify_dependencies(dependencies, available), prerequisites=[])
        for name in dependencies:
            if name.startswith('/runtime/'):
                relative = name.removeprefix('/runtime/')
                if not relative.startswith(('compiler/usr/lib/gcc/mipsel-linux-gnu/12/include/', 'sysroot/usr/include/')):
                    raise ValueError('Unexpected preprocessor input')
                item = dict(scope='runtime', **record(jail / name.lstrip('/'), relative, allow_empty=True))
            elif name in available:
                item = dict(scope='source', **record(directory / name, name, allow_empty=True))
            else:
                continue
            unit['prerequisites'].append(item)
        units.append(unit)
    report = dict(schema=1, source_sha256=SOURCE_SHA256, verified_runtime_inputs=verified,
                  candidate_count=len(units), source_only_closure_count=sum(unit['source_only_closure'] for unit in units),
                  distribution_approved_count=0, compiled_objects=0, units=units,
                  source_only_preprocessor_audit=True, rom_accessible=False,
                  renderer_inputs=False, copied_cooked_objects=False,
                  material_speedup_demonstrated=False, compiler_jobs=1,
                  reused_jail=str(jail), note='Permission block is independent of technical dependency closure')
    (out / 'audit.json').write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in (
        'candidate_count', 'source_only_closure_count', 'distribution_approved_count', 'compiled_objects')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--jail', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    audit(args.runtime, args.jail, args.out)
