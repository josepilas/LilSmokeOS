"""Crash/recovery tests using a QA-only binary. Never targets physical media."""
import json,os,subprocess,tempfile,zipfile
from pathlib import Path
import sys
binary=Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='lil-app-recovery-') as tmp:
    p=Path(tmp);root=p/'independent-apps';env=dict(os.environ,LILSMOKE_APPS_ROOT=str(root))
    def run(*args,crash=None):
        e=dict(env)
        if crash:e['LILSMOKE_APPS_FAILPOINT']=crash
        r=subprocess.run([str(binary),*map(str,args)],env=e,capture_output=True,text=True)
        assert r.returncode==(86 if crash else 0),(r.stdout,r.stderr,r.returncode)
    def pack(version,launcher):
        f=p/(version+'.lilpck')
        with zipfile.ZipFile(f,'w') as z:
            z.writestr('meta.json',json.dumps({'name':'Recovery','version':version,'id':'org.test.recovery'}));z.writestr(launcher,'#!/bin/sh\n')
        return f
    first=pack('1.0.0','one.sh');second=pack('2.0.0','two.sh')
    for point in ['staging','extracting','prepared','old-moved','new-moved']:
        run('install',first);(root/'Recovery/save.dat').write_text('keep')
        run('install',second,crash=point);run('recover')
        data=json.loads((root/'Recovery/meta.json').read_text())
        assert data['version']==('2.0.0' if point=='new-moved' else '1.0.0')
        assert (root/'Recovery/save.dat').read_text()=='keep'
        assert len(list((root/'Recovery').glob('*.sh')))==1
        run('uninstall','Recovery')
    large=p/'large.lilpck'
    with zipfile.ZipFile(large,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('meta.json',json.dumps({'name':'Recovery','version':'2.0.0','id':'org.test.recovery'}))
        z.writestr('run.sh','#!/bin/sh\n')
        with z.open('runtime/large.bin','w') as stream:
            for _ in range(128):stream.write(b'\0'*1024*1024)
    run('install',first)
    worker=subprocess.Popen([str(binary),'install',str(large)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    assert json.loads(worker.stdout.readline())['type']=='progress'
    worker.terminate();stdout,stderr=worker.communicate(timeout=20)
    assert worker.returncode==1,(stdout,stderr)
    run('recover')
    assert json.loads((root/'Recovery/meta.json').read_text())['version']=='1.0.0'
    assert not list((p/'.lilsmoke-apps').glob('stage-*'))
print('PASS five interruption recovery points and SIGTERM during extraction; original app/data preserved.')
