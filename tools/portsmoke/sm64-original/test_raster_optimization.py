"""Private renderer differential/performance checks; no physical FPS claim."""
import json
from pathlib import Path
import re
import subprocess
import statistics

HERE=Path(__file__).resolve().parent
source=Path(json.loads((HERE/'_build/private-workspace.json').read_text())['source'])
B=Path('/root/lilsmoke-os/build/buildroot/output')
out=Path('/root/sm64-raster-reference-20261011')
assert (out/'before.c').is_file()
includes=['-I'+str(source/'src/pc/gfx'),'-I'+str(source/'include'),'-I'+str(source/'src'),
          '-I'+str(source),'-I'+str(source/'build/original-host/include')]
defines=['-D_LANGUAGE_C','-DVERSION_US','-DNON_MATCHING','-DAVOID_UB','-DTARGET_LINUX','-DENABLE_SOFTRAST','-DF3DEX_GBI_2E','-DNO_SEGMENTED_MEMORY']
results={}
for target in ('host','mips'):
    for bits in (16,32):
        outputs={}
        for version,file in (('before',out/'before.c'),('after',source/'src/pc/gfx/gfx_soft.c')):
            binary=out/f'{target}-{bits}-{version}'
            cc='cc' if target=='host' else str(B/'host/bin/mipsel-buildroot-linux-musl-gcc')
            command=[cc,'-std=gnu11','-O3','-fno-fast-math','-fno-strict-aliasing',*includes,*defines,
                     '-DGFX_REFERENCE="'+str(file)+'"',str(HERE/'raster_benchmark.c'),
                     str(source/'src/pc/gfx/gfx_cc.c'),'-lm','-o',str(binary)]
            if bits==16:command+=['-DCONVERT']
            if target=='mips':command+=['-mips32r2','-mtune=74kc','-static']
            build=subprocess.run(command,capture_output=True,text=True)
            if build.returncode:
                raise RuntimeError(build.stderr)
            run=[str(binary),'40000'] if target=='host' else ['qemu-mipsel','-cpu','74Kf',str(binary),'8000']
            samples=[subprocess.check_output(run,text=True,timeout=120) for repeat in range(3)]
            text=''.join(samples)
            (out/f'{target}-{bits}-{version}.log').write_text(text)
            outputs[version]=text
        assert re.search(r'RASTER_EQ.*',outputs['before'])[0]==re.search(r'RASTER_EQ.*',outputs['after'])[0]
        times={version:{k:statistics.median(float(value) for observed,value in re.findall(r'RASTER_TIME hidden=(\d+) seconds=([\d.]+)',text) if int(observed)==k) for k in range(3)}
               for version,text in outputs.items()}
        results[f'{target}-{bits}']=dict(pixel_equivalent=True,times=times,
            relative_time={k:times['after'][k]/times['before'][k] for k in times['before']})
report=dict(passed=True,shader_variants=8,cases_per_format=10368,host_and_mips=True,
            framebuffer_bits=[16,32],physical_console_fps_measured=False,results=results)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
