import io,json,os,subprocess,tempfile,zipfile
from pathlib import Path
from PIL import Image
P=Path(__file__).resolve().parents[2]
Q=Path('/root/lilsmoke-applications-20261009')
out=Path(os.environ.get('LILSMOKE_APPS_QA_OUT',str(P/'test-results/lilsmoke-applications-style-2026-10-09/ui')))
out.mkdir(parents=True,exist_ok=True)
results=[]
for w,h in [(854,480),(640,480),(320,240)]:
    case=out/f'{w}x{h}';case.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lil-app-ui-',dir=Q) as tmp:
        root=Path(tmp)/'independent-apps';root.mkdir()
        archive=Path(tmp)/'example.lilpck'
        # Synthetic decoder/layout fixture, not an edited reference image.
        image=Image.new('RGB',(64,64),(37,145,217))
        for y in range(16,48):
            for x in range(16,48):image.putpixel((x,y),(238,247,255))
        data=io.BytesIO();image.save(data,format='PNG')
        icon=P/'tools/apps/demo/icon.png'
        if icon.is_file():data=io.BytesIO(icon.read_bytes())
        (Path(tmp)/'fixture-icon.png').write_bytes(data.getvalue())
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('meta.json',json.dumps(dict(id='org.lilsmoke.qa',name='Test Application',version='1.0.0',icon='icon.png')))
            z.writestr('run.sh','#!/bin/sh\nexit 0\n');z.writestr('icon.png',data.getvalue());z.writestr('runtime/data.bin',b'package data'*10000)
        versions=[]
        for version in ['2.0.0','0.5.0']:
            changed=Path(tmp)/(version+'.lilpck')
            with zipfile.ZipFile(changed,'w',zipfile.ZIP_DEFLATED) as z:
                z.writestr('meta.json',json.dumps(dict(id='org.lilsmoke.qa',name='Test Application',version=version,icon='icon.png')))
                z.writestr('updated.sh','#!/bin/sh\nexit 0\n');z.writestr('icon.png',data.getvalue())
            versions.append(str(changed))
        env=dict(os.environ,LILSMOKE_APPS_ROOT=str(root),LILSMOKE_APPS_HELPER=str(Q/'host-helper'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',LILSMOKE_UI_WINDOWED='1',ASAN_OPTIONS='detect_leaks=0')
        result=subprocess.run([str(Q/'ui-test'),str(w),str(h),str(archive),str(case),'/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',*versions],env=env,text=True,capture_output=True,timeout=180)
        (case/'run.log').write_text(result.stdout+result.stderr)
        if result.returncode and not os.environ.get('LILSMOKE_UI_SANITIZE'):
            trace=subprocess.run(['gdb','-q','-batch','-ex','run','-ex','bt','--args',str(Q/'ui-test'),str(w),str(h),str(archive),str(case),'/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',*versions],env=env,text=True,capture_output=True,timeout=120)
            (case/'backtrace.txt').write_text(trace.stdout+trace.stderr)
        assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
        assert (case/'grid-many.bmp').read_bytes()==(case/'grid-theme-independent.bmp').read_bytes(),'applications grid changed with theme'
        for file in case.glob('*.bmp'):
            frame=Image.open(file).convert('RGB');assert frame.size==(w,h)
            assert len(frame.getcolors(w*h))>20
            frame.save(file.with_suffix('.png'))
        results.append(dict(width=w,height=h,passed=True))
(out/'result.json').write_text(json.dumps(dict(passed=True,viewports=results,scope='real native SDL software views, helper-driven install/cancel/grid/rename/uninstall; host, no hardware FPS claim'),indent=2)+'\n')
print('PASS native application UI at TV, desktop and 320x240 viewports')
