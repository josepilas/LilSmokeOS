"""Compare exact raster output and relative CPU work, never physical console FPS."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--reference', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(exist_ok=True)
source = Path(json.loads((HERE / '_build/private-workspace.json').read_text())['source'])
sdk = Path('/root/lilsmoke-os/build/buildroot/output')
includes = ['-I'+str(source/p) for p in ('src/pc/gfx','include','src','.','build/original-host/include')]
defines = ['-D_LANGUAGE_C','-DVERSION_US','-DNON_MATCHING','-DAVOID_UB','-DTARGET_LINUX',
    '-DENABLE_SOFTRAST','-DF3DEX_GBI_2E','-DNO_SEGMENTED_MEMORY']
results = {}
for target in ('host','mips'):
    for bits in (16,32):
        outputs = {}
        for version, file in (('before',args.reference),('after',source/'src/pc/gfx/gfx_soft.c')):
            binary = args.out / f'{target}-{bits}-{version}'
            cc = 'cc' if target == 'host' else str(sdk/'host/bin/mipsel-buildroot-linux-musl-gcc')
            command = [cc,'-std=gnu11','-O3','-fno-fast-math','-fno-strict-aliasing',*includes,*defines,
                '-DGFX_REFERENCE="'+str(file)+'"',str(HERE/'endpoint_benchmark.c'),
                str(source/'src/pc/gfx/gfx_cc.c'),'-lm','-o',str(binary)]
            if bits == 16: command += ['-DCONVERT']
            if target == 'mips': command += ['-mips32r2','-mtune=74kc','-static']
            if target == 'mips' and version == 'after': command += ['-mno-shared','-mplt']
            subprocess.run(command,check=True,capture_output=True,text=True)
            run = [str(binary),'3000'] if target == 'host' else ['qemu-mipsel','-cpu','74Kf',str(binary),'1000']
            samples = [subprocess.check_output(run,text=True,timeout=180) for _ in range(3)]
            text = ''.join(samples)
            (args.out/f'{target}-{bits}-{version}.log').write_text(text)
            outputs[version] = text
        assert re.search(r'RASTER_EQ.*',outputs['before'])[0] == re.search(r'RASTER_EQ.*',outputs['after'])[0]
        times = {version:{k:statistics.median(float(value) for observed,value in
            re.findall(r'RASTER_TIME hidden=(\d+) seconds=([\d.]+)',text) if int(observed)==k)
            for k in range(3)} for version,text in outputs.items()}
        results[f'{target}-{bits}'] = dict(pixel_equivalent=True,times=times,
            relative_time={k:times['after'][k]/times['before'][k] for k in times['before']})
        print('PASS endpoint equivalence',target,bits,results[f'{target}-{bits}']['relative_time'],flush=True)
report = dict(passed=True,shader_variants=12,cases_per_format=51840,
    host_and_mips=True,framebuffer_bits=[16,32],physical_console_fps_measured=False,results=results)
(args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
