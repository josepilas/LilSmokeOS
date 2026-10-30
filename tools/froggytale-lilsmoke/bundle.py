#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Asset-free local package and exact matching source. No install/publication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
SDK = Path(os.environ.get('SDK', '/root/lilsmoke-os/build/buildroot/output'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--qualification',type=Path,required=True,help='Passing private FAT32 VM report for this exact binary')
    a=p.parse_args()
    build = HERE / '_build/mips'
    qualified=json.loads(a.qualification.read_text())
    assert qualified['passed'] and qualified['binary_sha256']==sha(build/'froggytale-lilsmoke'), 'Qualify this exact binary first'
    assert qualified['cache_filesystem']=='fat32' and qualified['cache_remount_verified']
    assert qualified['ram_mib']==128 and qualified['app_address_limit_mib']==112
    assert qualified['audio_requested'] and qualified['audio_frames']>0 and qualified['nonzero_pcm_frames']>0
    assert qualified['owner_original_unchanged'] and qualified['owner_wad_and_oggs_unchanged']
    assert qualified['owner_export_read_only'] and qualified['program_export_read_only'] and not qualified['sd_touched']
    assert [c['name'] for c in qualified['cases']]==['first','cached','runtime']
    assert all(c['exit_code']==0 and 0<c['peak_as_kib']<=112*1024 for c in qualified['cases'])
    out = HERE / '_build/package'
    out.mkdir(parents=True, exist_ok=False)
    root = out / 'app'
    shutil.copytree(HERE/'app', root)
    (root/'runtime/bin').mkdir(parents=True)
    (root/'runtime/lib').mkdir()
    (root/'licenses').mkdir()
    binary = root/'runtime/bin/froggytale-lilsmoke'
    build_info=json.loads((build/'build.json').read_text())
    assert sha(build/'froggytale-lilsmoke')==build_info['binary_sha256']
    for item in build_info['sources']:
        path=item['path']
        source=HERE/'src'/Path(path).name if path.startswith('adapter/') else build/('audio' if path.startswith('audio/') else 'stage')/ (Path(path).name if path.startswith('audio/') else path)
        assert sha(source)==item['sha256'], 'Rebuild after source edits: '+path
    with tempfile.TemporaryDirectory(prefix='audio-source-check-',dir=out) as temp:
        subprocess.run(['python3','-B',str(HERE/'audio/stage.py'),'--upstream',str(build/'stage'),'--out',temp],check=True)
        for name in ('ma_audio_system.c','ma_audio_system.h'):
            assert sha(Path(temp)/name)==sha(build/'audio'/name), 'Rebuild after audio edits: '+name
    shutil.copyfile(build/'froggytale-lilsmoke', binary)
    lib = root/'runtime/lib/libSDL2-2.0.so.0'
    shutil.copyfile((SDK/'target/usr/lib/libSDL2-2.0.so.0').resolve(strict=True), lib)
    cc=SDK/'host/bin/mipsel-buildroot-linux-musl-gcc'
    atomic_source=Path(subprocess.check_output([str(cc),'-print-file-name=libatomic.so.1'],text=True).strip()).resolve(strict=True)
    atomic=root/'runtime/lib/libatomic.so.1'
    shutil.copyfile(atomic_source,atomic)
    subprocess.run([str(SDK/'host/bin/mipsel-buildroot-linux-musl-strip'), '--strip-unneeded', str(binary), str(lib),str(atomic)], check=True)
    readelf = str(SDK/'host/bin/mipsel-buildroot-linux-musl-readelf')
    elf = subprocess.check_output([readelf, '-l', '-A', '-d', str(binary)], text=True)
    deps = subprocess.check_output([readelf, '-d', str(lib),str(atomic)], text=True)
    assert '/lib/ld-musl-mipsel.so.1' in elf and 'Hard float' in elf
    assert 'libGL' not in elf+deps and 'libEGL' not in elf+deps
    # SDK SDL2 has only the OS musl dependency; refuse unexpected closure changes.
    import re
    assert set(re.findall(r'Shared library: \[(.*?)\]', elf+deps)) <= {'libc.so', 'libSDL2-2.0.so.0','libatomic.so.1'}
    shutil.copyfile(HERE/'_build/upstream/LICENSE', root/'licenses/AGPL-3.0.txt')
    shutil.copyfile(SDK/'build/sdl2-2.28.5/LICENSE.txt', root/'licenses/SDL2-zlib.txt')
    gcc=SDK/'build/host-gcc-final-12.3.0'
    shutil.copyfile(gcc/'COPYING3',root/'licenses/GCC-GPL-3.0.txt')
    shutil.copyfile(gcc/'COPYING.RUNTIME',root/'licenses/GCC-Runtime-Exception.txt')
    (root/'licenses/NOTICE.txt').write_text(
        'Modified FroggyTale / Butterscotch, AGPLv3; original authors retain copyrights.\n'
        'FroggyTale pin: 5699418f671dc8bb07dc2e4adc60169b616ab906.\n'
        'LilSmoke adapter and build scripts: AGPL-3.0-only, 2026.\n'
        'gamepad-state.h: retained MIT notice, included with corresponding source.\n'
        'SDL2 2.28.5: zlib license. OS musl loader/libc is not bundled or replaced.\n'
        'Vendor licenses including stb, base64, bzip2, miniz, MD5/SHA1 remain in source.\n'
        'Bundled GCC 12.3 libatomic: GPLv3 with GCC Runtime Library Exception 3.1.\n'
        'miniaudio and stb_vorbis: original license notices retained in corresponding source.\n'
        'No separately licensed OS/UI source, proprietary game data, derived atlas or firmware in this package.\n')
    (root/'QUALIFICATION.txt').write_text(
        'LOCAL EXPERIMENTAL QUALIFICATION; NOT A PUBLIC RELEASE\n'
        'MIPS32r2/o32 hard-float musl; QEMU Malta 74Kf, one CPU, 128 MiB RAM.\n'
        'Application RLIMIT_AS 112 MiB; CPU software renderer; no 3D GPU.\n'
        'Private owner WAD/OGGs read in place through read-only 9p; all unchanged.\n'
        'Cache and save paths on a temporary 256 MiB FAT32 virtio block disk,\n'
        'hosted on Linux ext4, not NTFS/9p. Disk removed after the test.\n'
        'First preparation exited 0 and released its preparation lock.\n'
        'FAT32 unmount/remount followed by sealed-metadata cached preparation.\n'+
        ''.join(f"{c['name']}: exit={c['exit_code']} guest_seconds={c['elapsed_seconds']} peak_as_kib={c['peak_as_kib']} peak_rss_kib={c['peak_rss_kib']}\n" for c in qualified['cases'])+
        f"SDL dummy sink, automatic callback: frames={qualified['audio_frames']} nonzero_pcm_frames={qualified['nonzero_pcm_frames']}.\n"
        'Runtime reached its requested frame exit; this is an opening smoke test.\n'
        'NOT a complete playthrough, physical hardware, speaker/HDMI/input,\n'
        'handheld performance or real save/load gameplay qualification.\n'
        'No SD card was written. No owner game data is included.\n')
    stem = 'Undertale-LilSmoke-0.1.0'
    source = out/(stem+'-corresponding-source.zip')
    with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(HERE.rglob('*')):
            rel = path.relative_to(HERE)
            if path.is_file() and '_build' not in rel.parts and '__pycache__' not in rel.parts:
                z.write(path, 'tools/froggytale-lilsmoke/'+rel.as_posix())
        for path in sorted((HERE/'_build/upstream').rglob('*')):
            if path.is_file():
                z.write(path, 'tools/froggytale-lilsmoke/_build/upstream/'+path.relative_to(HERE/'_build/upstream').as_posix())
        z.write(build/'native/gamepad-state.h', 'package/lilsmoke-ui/src/src/gamepad-state.h')
        z.write(build/'native/font8x8_basic.h', 'package/portsmoke-sm64/src/vendor/tinygl/src/font8x8_basic.h')
        z.write(build/'build.json', 'build-evidence.json')
        for name in ('mips_boot_smoke.py','mips_disk_fixture.py'):
            z.write(PROJECT/'tools/hwtest'/name,'tools/hwtest/'+name)
        z.writestr('REBUILD.txt', 'With the existing compatible GCC 12.3/musl MIPS SDK:\n'
            'SDK=/path/to/sdk/output FROGGY_CPU=1 python3 -B tools/froggytale-lilsmoke/build.py\n'
            'Pinned source is present; fetch.py verifies offline against its inventory.\n'
            'No network, game data, SF2000 toolchain or SDK download is needed.\n')
    (root/'SOURCE-OFFER.txt').write_text(
        f'The exact corresponding source accompanies this local package as {source.name}.\n'
        f'SHA256 {sha(source)}\n'
        'It includes the pinned source subset, all modified adapter sources, source inventory,\n'
        'build script, staging transformation, tests, packer and MIT controller transport.\n'
        'No game files or private OS/UI code. Use REBUILD.txt; set SDK to your musl SDK.\n'
        'This local artifact has not been published. Convey this source archive together\n'
        'with the application, or provide equivalent corresponding-source access.\n'
        'An upstream URL alone is NOT the corresponding source of this modified build.\n')
    package = out/(stem+'.lilpck')
    with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path in sorted(root.rglob('*')):
            if path.is_file(): z.write(path,path.relative_to(root).as_posix())
    with zipfile.ZipFile(package) as z:
        names=z.namelist()
        assert [n for n in names if '/' not in n and n.endswith('.sh')]==['run.sh']
        assert not any(n.startswith(('data/','original/','cache/','saves/')) or n.lower().endswith(('.unx','.win','.ogg','.rgba4444','.exe','.dll')) for n in names)
    report=dict(package=dict(path=package.name,bytes=package.stat().st_size,sha256=sha(package)),
        source=dict(path=source.name,bytes=source.stat().st_size,sha256=sha(source)),
        binary_sha256=sha(binary),files=names,physical_hardware_tested=False)
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    (out/'elf.txt').write_text(elf+'\nSDL2:\n'+deps)
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
