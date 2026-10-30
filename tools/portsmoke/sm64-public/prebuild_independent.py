#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Rebuild the conservative object allowlist in a fresh, unprivileged chroot.

Linux/root, QEMU user binfmt and a frozen runtime are required. No game ROM,
extracted asset, cooked object, SDK target, or full-build snapshot is an input.
The optional oracle reads only dependency text, never object contents.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import time

from independent_objects import (
    BUILD, ELIGIBLE, SCHEMA, SOURCE_INPUTS, TOOL_LIMIT, normalized_flags, read_regular,
    fingerprint, record, safe_name, sha, verify_object,
)


PIN = 'a173bd0a3eeed9b064a234e528f128424d46ea08'
SOURCE_SHA256 = 'ce46664d572d8567e6b2eb0febcb51b5597d4a0115c8b458fc8e852eb4f30212'
DISTRIBUTION_SHA256 = '40208d4e30fe6370b1b99b5a9cf9845a485f26d3e6131e477ab1305a910999c9'


def verify_runtime_inputs(runtime):
    data = read_regular(runtime / 'distribution-manifest.json', 2 * 1024 * 1024)
    if sha(data) != DISTRIBUTION_SHA256:
        raise ValueError('Unexpected frozen distribution manifest')
    listing = json.loads(data)['files']
    wanted = ['native_build.py', 'recipe.json', 'source.tar.gz', 'source-material-review.json',
              'licenses/NOTICE.txt', 'licenses/sm64-base-CC0.txt', 'licenses/MPL-2.0.txt',
              'sysroot/lib/libc.so']
    for base in ('compiler', 'sysroot/usr/include'):
        wanted.extend(path.relative_to(runtime).as_posix()
                      for path in (runtime / base).rglob('*') if path.is_file())
    for name in wanted:
        path = runtime / name
        if path.is_symlink() or path.resolve() != path:
            raise ValueError('Symlink in frozen input: ' + name)
        if fingerprint(path, TOOL_LIMIT, allow_empty=True) != listing.get('runtime/' + name):
            raise ValueError('Frozen distribution input changed: ' + name)
    return len(wanted)


def production_flags(runtime):
    """Read the literal production argv without importing or executing it."""
    tree = ast.parse(read_regular(runtime / 'native_build.py', 256 * 1024))
    compiler = next(node for node in ast.walk(tree)
                    if isinstance(node, ast.FunctionDef) and node.name == 'compile')
    assignments = [node.value for node in ast.walk(compiler)
                   if isinstance(node, ast.Assign) and
                   any(isinstance(target, ast.Name) and target.id == 'flags' for target in node.targets)]
    if len(assignments) != 1:
        raise ValueError('Cannot identify production flags')

    def evaluate(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.List):
            return [evaluate(x) for x in node.elts]
        if isinstance(node, ast.Name) and node.id == 'sysroot':
            return runtime / 'sysroot'
        if (isinstance(node, ast.Attribute) and node.attr == 'runtime' and
                isinstance(node.value, ast.Name) and node.value.id == 'self'):
            return runtime
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Div)):
            left, right = evaluate(node.left), evaluate(node.right)
            return left + right if isinstance(node.op, ast.Add) else left / right
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and
                node.func.id == 'str' and len(node.args) == 1 and not node.keywords):
            return str(evaluate(node.args[0]))
        raise ValueError('Unsupported production flag expression')

    return evaluate(assignments[0])


def parse_dependencies(text):
    text = text.replace('\\\n', ' ')
    first = text.splitlines()[0]
    if ':' not in first:
        raise ValueError('Missing dependency target')
    target, rest = first.split(':', 1)
    dependencies = shlex.split(rest, comments=False, posix=True)
    if any(any(c in token for c in ':;$=') for token in dependencies):
        raise ValueError('Unsupported Make dependency syntax')
    return target, sorted(set(dependencies))


def extract_inputs(archive, destination):
    contents, names = {}, set()
    total = 0
    if sha(read_regular(archive, 32 * 1024 * 1024)) != SOURCE_SHA256:
        raise ValueError('Pinned source archive checksum mismatch')
    with tarfile.open(archive, 'r:gz') as source:
        for member in source:
            name = safe_name(member.name.rstrip('/'))
            if name.casefold() in names or not (member.isfile() or member.isdir()):
                raise ValueError('Duplicate or linked archive member')
            names.add(name.casefold())
            total += member.size
            if len(names) > 10000 or member.size < 0 or total > 128 * 1024 * 1024:
                raise ValueError('Source archive exceeds budget')
            if member.isfile() and name in SOURCE_INPUTS:
                data = source.extractfile(member).read()
                if len(data) != member.size:
                    raise ValueError('Truncated archive member')
                contents[name] = data
    if contents.keys() != SOURCE_INPUTS:
        raise ValueError('Missing explicitly approved source input')
    if b'Mozilla Public License' not in contents['lilsmoke/LICENSE']:
        raise ValueError('Unexpected adaptation licence')
    for name, data in contents.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(0o444)
    return contents, names


def copy_regular(source, destination):
    if source.is_symlink() or not source.is_file():
        raise ValueError('Unexpected toolchain link or nonfile: ' + str(source))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o555 if source.stat().st_mode & 0o111 else 0o444)


def prepare_jail(runtime, jail):
    jail.mkdir()
    for base in ('compiler', 'sysroot/usr/include'):
        for path in sorted((runtime / base).rglob('*')):
            if path.is_symlink():
                raise ValueError('Toolchain symlink: ' + str(path))
            if path.is_file():
                copy_regular(path, jail / 'runtime' / path.relative_to(runtime))
    # Only the musl interpreter is needed, not SDK libraries, CRT objects,
    # existing game objects, Python, or any application/cache directory.
    copy_regular(runtime / 'sysroot/lib/libc.so', jail / 'runtime/sysroot/lib/libc.so')
    copy_regular(runtime / 'sysroot/lib/libc.so', jail / 'runtime/sysroot/lib/ld-musl-mipsel.so.1')
    qemu = Path('/usr/bin/qemu-mipsel')
    copy_regular(qemu, jail / 'usr/bin/qemu-mipsel')
    registration = Path('/proc/sys/fs/binfmt_misc/qemu-mipsel').read_text()
    match = re.search(r'^interpreter (/[^\n]+)$', registration, re.MULTILINE)
    if not match or 'enabled' not in registration:
        raise ValueError('QEMU MIPS binfmt registration is required')
    copy_regular(qemu, jail / match.group(1).lstrip('/'))
    dependencies = subprocess.check_output(['ldd', str(qemu)], text=True)
    for line in dependencies.splitlines():
        match = re.search(r'(/[^ ]+) \(', line)
        if match:
            dependency = Path(match.group(1))
            copy_regular(dependency.resolve(), jail / dependency.relative_to('/'))
    (jail / 'tmp').mkdir(mode=0o777)
    (jail / 'tmp').chmod(0o777)
    (jail / 'source/build/original-mips').mkdir(parents=True)
    (jail / 'source/build/original-mips').chmod(0o777)


def run_compile(jail, argv, log, working_directory='/source'):
    def isolate():
        os.chroot(jail)
        os.chdir(working_directory)
        os.setgroups([])
        os.setgid(65534)
        os.setuid(65534)

    environment = dict(PATH='/runtime/compiler/wrappers:/usr/bin:/bin',
                       HOME='/tmp', TMPDIR='/tmp', LC_ALL='C',
                       QEMU_CPU='74Kf', QEMU_LD_PREFIX='/runtime/sysroot',
                       MAKEFLAGS='-j1', OMP_NUM_THREADS='1')
    with log.open('wb') as handle:
        subprocess.run(['/usr/bin/qemu-mipsel', '/runtime/compiler/wrappers/cc', *argv],
                       env=environment, preexec_fn=isolate, stdout=handle,
                       stderr=subprocess.STDOUT, check=True, timeout=600)


def graph_audit(oracle, archive_names):
    """Read dependency metadata only. Never stat/open a cooked object or asset."""
    result = dict(oracle_used=False, examined=0, categories={})
    if oracle is None:
        return result
    if oracle.is_symlink() or not oracle.is_dir():
        raise ValueError('Unsafe dependency oracle')
    result['oracle_used'] = True
    categories = {}
    for path in sorted(oracle.rglob('*.d')):
        target, deps = parse_dependencies(read_regular(path, 256 * 1024).decode())
        if not target.endswith('.o'):
            continue
        reasons = []
        source = next((dep for dep in deps if dep.endswith('.c') and not dep.endswith('.inc.c')), None)
        if any(dep.startswith('src/pc/gfx/') for dep in deps):
            reasons.append('source-only graphics licence')
        if any(dep.startswith('include/PR/') for dep in deps):
            reasons.append('SGI proprietary header notice; no clearance')
        if any(dep.startswith(('build/', 'actors/', 'levels/', 'textures/', 'bin/', 'sound/', 'text/'))
               and not dep.endswith('.h') for dep in deps):
            reasons.append('asset/data-bearing or generated prerequisite')
        if any(dep.casefold() not in archive_names for dep in deps):
            reasons.append('not solely pinned source-archive inputs')
        if source not in ELIGIBLE:
            reasons.append('not in explicitly licensed adaptation allowlist')
        key = '; '.join(reasons) or 'approved adaptation'
        categories.setdefault(key, []).append(target)
        result['examined'] += 1
    result['categories'] = categories
    return result


def build(runtime, out, oracle=None):
    runtime = runtime.absolute()
    out = out.absolute()
    if os.geteuid() != 0 or out.parent != Path('/root') or not out.name.startswith('sm64-independent-'):
        raise ValueError('Run as root with fresh /root/sm64-independent-* output')
    # Refuse aliases and existing outputs. This builder never deletes any tree.
    if runtime.resolve() != runtime or out.exists() or out.is_symlink():
        raise ValueError('Runtime must be canonical and output must be fresh')
    verified_files = verify_runtime_inputs(runtime)
    recipe = json.loads(read_regular(runtime / 'recipe.json', 65536))
    if recipe['source_pin'] != PIN or recipe['source_sha256'] != SOURCE_SHA256:
        raise ValueError('Unexpected recipe source identity')
    flags = production_flags(runtime)
    out.mkdir(mode=0o755)
    jail = out / 'jail'
    prepare_jail(runtime, jail)
    contents, names = extract_inputs(runtime / 'source.tar.gz', jail / 'source')
    bundle = out / 'bundle'
    (bundle / 'objects').mkdir(parents=True)
    (out / 'logs').mkdir()
    normalized = normalized_flags(flags, runtime)
    sandbox_flags = [x.replace('@runtime', '/runtime') for x in normalized]
    entries, timings = [], {}
    for source, (leaf, extra) in sorted(ELIGIBLE.items()):
        target = BUILD + leaf
        depfile = target[:-2] + '.d'
        argv = [*sandbox_flags, *extra, '-c', source, '-MD', '-MP', '-MT', target, '-MF', depfile, '-o', target]
        previous = None
        started = time.monotonic()
        for attempt in (1, 2):
            # Force independent code generation twice, never load an oracle object.
            (jail / 'source' / target).unlink(missing_ok=True)
            run_compile(jail, argv, out / 'logs' / (leaf + '.' + str(attempt) + '.log'))
            data = read_regular(jail / 'source' / target)
            verify_object(data)
            if previous is not None and previous != data:
                raise ValueError('Non-reproducible object: ' + source)
            previous = data
        timings[source] = round(time.monotonic() - started, 3)
        actual_target, dependencies = parse_dependencies(read_regular(jail / 'source' / depfile, 256 * 1024).decode())
        if actual_target != target:
            raise ValueError('Compiler dependency target mismatch')
        prerequisites = []
        dependencies = sorted(set(dependencies + ['lilsmoke/LICENSE', 'lilsmoke/public.mk']))
        for dependency in dependencies:
            if dependency.startswith('/runtime/'):
                name = dependency.removeprefix('/runtime/')
                if not name.startswith(('sysroot/usr/include/', 'compiler/usr/lib/gcc/mipsel-linux-gnu/12/include/')):
                    raise ValueError('Unexpected compiler prerequisite: ' + dependency)
                item = record(jail / dependency.lstrip('/'), name, allow_empty=True)
                item['scope'] = 'runtime'
            else:
                if dependency not in SOURCE_INPUTS or contents[dependency] != read_regular(jail / 'source' / dependency):
                    raise ValueError('Non-audited source prerequisite: ' + dependency)
                item = record(jail / 'source' / dependency, dependency)
                item['scope'] = 'source'
            prerequisites.append(item)
        object_path = bundle / 'objects' / leaf
        object_path.write_bytes(previous)
        entries.append(dict(source=source, target=target, extra_flags=extra,
                            object=record(object_path, 'objects/' + leaf), prerequisites=prerequisites))
    tools = []
    for path in sorted((runtime / 'compiler').rglob('*')):
        if path.is_file():
            name = path.relative_to(runtime).as_posix()
            # Include binaries, dynamic libraries and wrappers; header contents
            # are already per-object prerequisites. No target/SDK hash shortcut.
            if '/include/' not in name and '/share/' not in name:
                tools.append(record(path, name, TOOL_LIMIT))
    manifest = dict(schema=SCHEMA, source_pin=PIN, source_sha256=SOURCE_SHA256,
                    flags=normalized, toolchain=tools, objects=entries,
                    provenance=dict(distribution_manifest_sha256=DISTRIBUTION_SHA256,
                                    verified_runtime_inputs=verified_files),
                    audit=dict(distribution_scope='MPL-2.0 LilSmoke adaptations only',
                               rom_present=False, extracted_assets_present=False,
                               full_build_binary_inputs=False, renderer_inputs=False,
                               sandbox='fresh chroot; uid/gid 65534; no host mounts',
                               repeat_builds=2, compile_jobs=1))
    manifest_path = bundle / 'manifest.json'
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n', encoding='ascii')
    (bundle / 'LICENSE').write_bytes(contents['lilsmoke/LICENSE'])
    audit = dict(manifest_sha256=sha(manifest_path.read_bytes()), count=len(entries),
                 object_bytes=sum(x['object']['bytes'] for x in entries),
                 source_sha256=SOURCE_SHA256, timings_two_builds_host_qemu_seconds=timings,
                 verified_runtime_inputs=verified_files,
                 distribution_manifest_sha256=DISTRIBUTION_SHA256,
                 material_first_boot_speedup_demonstrated=False,
                 broad_engine_distribution_blocked=True,
                 blockers=['Pinned fork has no root licence grant.',
                           'Generic engine dependencies include SGI proprietary PR headers.',
                           'Graphics source licence forbids binary redistribution.',
                           'ROM-derived/generated/asset-bearing prerequisites excluded.'],
                 graph=graph_audit(oracle, names))
    (out / 'audit.json').write_text(json.dumps(audit, sort_keys=True, indent=2) + '\n', encoding='ascii')
    for path in bundle.rglob('*'):
        path.chmod(0o555 if path.is_dir() else 0o444)
    bundle.chmod(0o555)
    print(json.dumps(dict(bundle=str(bundle), manifest_sha256=audit['manifest_sha256'],
                          count=audit['count'], object_bytes=audit['object_bytes']), sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--dependency-oracle', type=Path)
    args = parser.parse_args()
    build(args.runtime, args.out, args.dependency_oracle)


if __name__ == '__main__':
    main()
