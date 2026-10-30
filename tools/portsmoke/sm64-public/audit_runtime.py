#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check the musl runtime closure, including conversion tools and Python modules."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


def audit(app, sdk, install=False):
    runtime = app / 'runtime'
    readelf = sdk / 'host/bin/mipsel-buildroot-linux-musl-readelf'
    dependencies = {}
    missing = set()
    with tempfile.TemporaryDirectory(prefix='sm64-tool-audit-') as scratch:
        pending = [runtime / 'bin/sm64-prepare', runtime / 'python/usr/bin/python3.11']
        pending += list((runtime / 'python/usr/lib').rglob('*.so'))
        with tarfile.open(runtime / 'source.tar.gz', 'r:gz') as archive:
            for member in archive:
                if member.isfile() and member.name.startswith('tools/'):
                    handle = archive.extractfile(member)
                    data = handle.read()
                    if data.startswith(b'\x7fELF'):
                        target = Path(scratch) / str(len(pending))
                        target.write_bytes(data)
                        pending.append(target)
        scanned = set()
        while pending:
            binary = pending.pop()
            key = str(binary.resolve())
            if key in scanned:
                continue
            scanned.add(key)
            result = subprocess.check_output([str(readelf), '-d', str(binary)], text=True)
            for name in re.findall(r'\(NEEDED\).*\[([^]]+)\]', result):
                if name == 'libc.so' or name in dependencies:
                    continue
                bundled = runtime / 'lib' / name
                source = next((base / name for base in (sdk / 'target/usr/lib', sdk / 'target/lib')
                    if (base / name).is_file()), None)
                if source is None:
                    missing.add(name)
                    continue
                dependencies[name] = dict(bundled=bundled.is_file(), source=str(source.resolve()))
                if install and not bundled.is_file():
                    shutil.copyfile(source, bundled)
                    dependencies[name]['bundled'] = True
                pending.append(bundled if bundled.is_file() else source)
    return dict(passed=not missing and all(row['bundled'] for row in dependencies.values()),
        dependencies=dependencies, unavailable=sorted(missing), scanned_elf_files=len(scanned),
        system_contract=['musl loader/libc', 'DirectFB framebuffer backend and platform drivers'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--install-missing', action='store_true')
    args = parser.parse_args()
    result = audit(args.app, args.sdk, args.install_missing)
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
