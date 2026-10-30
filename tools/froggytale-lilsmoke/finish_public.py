#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Record sanitized proof, freeze archives, then run off-card MIPS installer QA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE/'_build/public-0.0.1'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path,value):path.write_text(json.dumps(value,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
    sdk=a.sdk.resolve();build=HERE/'_build/public-engine-build-0.0.1'
    proof=json.loads((build/'report.json').read_text())
    qa_dir=HERE/'_build/public-rebuilt-qa-0.0.1';qa=json.loads((qa_dir/'report.json').read_text())
    assert proof['passed'] and proof['engine_rebuilt'] and qa['passed']
    assert proof['binary_sha256']==sha(build/'froggytale-lilsmoke')==qa['binary_sha256']
    commands=json.loads((build/'commands.json').read_text())
    audio=[c for c in commands if any(x.endswith('/audio/ma_audio_system.c') for x in c)]
    assert len(audio)==1 and '-fno-tree-vrp' in audio[0]
    assert sum('-fno-tree-vrp' in c for c in commands)==1
    readelf=sdk/'host/bin/mipsel-buildroot-linux-musl-readelf'
    elf=subprocess.check_output([str(readelf),'-h','-l','-d',str(build/'froggytale-lilsmoke')],text=True)
    needed=sorted(set(re.findall(r'Shared library: \[([^]]+)\]',elf)))
    assert needed==['libSDL2-2.0.so.0','libatomic.so.1','libc.so']
    assert '/lib/ld-musl-mipsel.so.1' in elf and 'mips32r2' in elf and 'o32' in elf
    selftest=(qa_dir/'selftest.log').read_text()
    assert 'checks=80 failed=0' in selftest
    proof.update(build_script_sha256=sha(ROOT/'build.py'),source_plan_sha256=sha(ROOT/'build-plan.json'),
        audio_VRP_workaround_per_TU=True,compile_and_link_commands=len(commands),
        elf=dict(ISA='MIPS32r2',ABI='o32 hard-float musl',loader='/lib/ld-musl-mipsel.so.1',needed=needed),
        synthetic_cases=qa['results'],selftest_checks=80,new_gameplay_test=False,SDK_modified=False)
    write(ROOT/'evidence/public-build.json',proof)
    dep=HERE/'_build/public-dep-checks'
    assert all((dep/name/'config.status').is_file() for name in ('libatomic','sdl2'))
    write(ROOT/'evidence/dependency-source-check.json',dict(passed=True,configure_only=True,
        libatomic_source_closure_configures=True,SDL2_source_closure_configures=True,
        SDL2_profile='source-completeness check, video/audio disabled; not production backend qualification',
        dependency_binaries_rebuilt=False,SDK_modified=False))
    cpu='1' if 1 in os.sched_getaffinity(0) else str(min(os.sched_getaffinity(0)))
    prefix=['taskset','-c',cpu,'nice','-n','15','python3','-B']
    subprocess.run(prefix+[str(ROOT/'tools/audit.py'),'--write-manifest'],check=True)
    out=HERE/'_build/public-release-0.0.1'
    subprocess.run(prefix+[str(ROOT/'tools/release.py'),'--runtime',str(HERE/'_build/package/app/runtime'),'--out',str(out)],check=True)
    env=dict(os.environ,SDK=str(sdk),FROGGY_CPU=cpu)
    package=out/'Undertale-LilSmoke-0.0.1.lilpck';source=out/'Undertale-LilSmoke-0.0.1-corresponding-source.zip'
    qa_out=HERE/'_build/public-package-qa-0.0.1'
    with (out/'package-qa-runner.log').open('w') as log:
        subprocess.run(prefix+[str(ROOT/'test_package.py'),'--package',str(package),'--source',str(source),
            '--out',str(qa_out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    package_qa=json.loads((qa_out/'report.json').read_text());assert package_qa['passed']
    shutil.copyfile(qa_out/'report.json',out/'package-qa.json')
    manifest=json.loads((ROOT/'SOURCE-MANIFEST.json').read_text())
    handoff=dict(frozen=True,tag='0.0.1',title='0.0.1',published=False,SD_writes=False,
        source_file_count=manifest['file_count']+1,source_payload_bytes=manifest['bytes'],
        source_manifest_sha256=sha(ROOT/'SOURCE-MANIFEST.json'),release_manifest_sha256=sha(out/'manifest.json'),
        package_qa_sha256=sha(out/'package-qa.json'),qualified_engine_sha256=proof['package_engine_sha256'],
        public_rebuild_sha256=proof['binary_sha256'],public_build_passed=True,package_QA_passed=True,
        assets_included=False,private_host_paths_in_source=False,license_boundary='AGPL runtime; independent audit/release tools MPL-2.0',
        artifacts={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in (package,source)})
    write(out/'HANDOFF.json',handoff)
    published=[package,source,out/'manifest.json',out/'package-qa.json',out/'HANDOFF.json']
    (out/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in published))
    subprocess.run(prefix+[str(ROOT/'tools/audit.py')],check=True)
    print(json.dumps(handoff,indent=2))


if __name__=='__main__':main()
