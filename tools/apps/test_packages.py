"""Host/MIPS package lifecycle and hostile-archive tests. No physical media."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile

parser=argparse.ArgumentParser()
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--qemu',action='store_true')
parser.add_argument('--sysroot',type=Path)
parser.add_argument('--work-dir',type=Path)
args=parser.parse_args()
command=[str(args.binary)]
if args.qemu:command=['qemu-mipsel','-cpu','74Kf','-L',str(args.sysroot),*command]
count=0
with tempfile.TemporaryDirectory(prefix='lilsmoke-apps-test-',dir=args.work_dir) as folder:
    base=Path(folder);root=base/'independent-apps'
    env=dict(os.environ,LILSMOKE_APPS_ROOT=str(root))
    if args.qemu:env['LD_LIBRARY_PATH']=str(args.sysroot/'usr/lib')+':'+str(args.sysroot/'lib')
    def call(*words,ok=True):
        global count
        result=subprocess.run([*command,*map(str,words)],env=env,text=True,capture_output=True,timeout=40)
        assert (result.returncode==0)==ok,(words,result.stdout,result.stderr)
        count+=1
        return [json.loads(s) for s in result.stdout.splitlines() if s.startswith('{')]
    def package(label,version='1.0.0',script='uno.sh',extra=None,fmt='zip',name='Test App',identity='org.lilsmoke.test'):
        path=base/(label+'.lilpck')
        members={'meta.json':json.dumps(dict(name=name,version=version,id=identity)).encode(),script:b'#!/bin/sh\nprintf "LAUNCHED:%s:%s\\n" "$LILSMOKE_APP_NAME" "$LILSMOKE_APP_DIR"\n',
                 'runtime/engine':b'engine','runtime/helper.sh':b'#!/bin/sh\n','runtime/other.sh':b'#!/bin/sh\n'}
        if extra:members.update(extra)
        if fmt=='zip':
            with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as out:
                for key,data in members.items():out.writestr(key,data)
        else:
            with tarfile.open(path,'w:gz' if fmt=='gz' else 'w') as out:
                for key,data in members.items():
                    item=tarfile.TarInfo('./'+key);item.size=len(data);out.addfile(item,io.BytesIO(data))
        return path
    one=package('one',extra={'runtime/stale.bin':b'stale','settings.ini':b'old'})
    original=hashlib.sha256(one.read_bytes()).hexdigest()
    info=call('inspect',one,base/'preview')[-1]
    assert info['relation']=='new' and info['name']=='Test App'
    assert not list(root.iterdir()),'preview/cancel installed files'
    results=call('install',one,info['sha256'])
    assert results[-1]['percent']==100 and results[-1]['processed']==results[-1]['total']
    app=root/'Test App';assert (app/'uno.sh').exists()
    (app/'saves').mkdir();(app/'saves/player.sav').write_bytes(b'personal save')
    (app/'user-notes.txt').write_text('keep')
    (app/'empty-user-data').mkdir()
    (app/'Runtime').mkdir(exist_ok=True)
    (app/'Runtime/New.bin').write_bytes(b'user collision')
    two=package('two','2.0.0','duo.sh',extra={'settings.ini':b'new','runtime/new.bin':b'new'})
    assert call('inspect',two,base/'preview2')[-1]['relation']=='newer'
    call('install',two)
    assert not (app/'uno.sh').exists() and (app/'duo.sh').exists()
    assert not (app/'runtime/stale.bin').exists() and (app/'runtime/new.bin').exists()
    assert (app/'settings.ini').read_bytes()==b'new'
    assert (app/'saves/player.sav').read_bytes()==b'personal save' and (app/'user-notes.txt').read_text()=='keep'
    assert (app/'empty-user-data').is_dir()
    assert call('inspect',two,base/'preview2')[-1]['relation']=='same'
    call('install',two,ok=False)
    assert call('inspect',one,base/'preview1')[-1]['relation']=='older'
    call('rename','Test App','My Renamed App')
    app=root/'My Renamed App';assert not (root/'Test App').exists()
    assert json.loads((app/'meta.json').read_text())['name']=='My Renamed App'
    assert call('inspect',one,base/'preview1')[-1]['installed_name']=='My Renamed App'
    call('install',one)
    assert json.loads((app/'meta.json').read_text())['name']=='My Renamed App' and (app/'uno.sh').exists()
    assert not (app/'duo.sh').exists() and (app/'saves/player.sav').exists()
    assert call('list')[-1]['apps'][0]['name']=='My Renamed App'
    call('uninstall','My Renamed App');assert not list(root.iterdir())
    abandoned=base/'.lilsmoke-apps/stage-999999'
    abandoned.mkdir();(abandoned/'partial.dat').write_text('unfinished install')
    call('recover');assert not abandoned.exists()
    assert hashlib.sha256(one.read_bytes()).hexdigest()==original
    for fmt in ['gz','tar']:
        pack=package(fmt,fmt=fmt);call('install',pack);call('uninstall','Test App')
    # Archive type is determined by magic, even though every filename is .lilpck.
    import shutil
    if shutil.which('7z'):
        content=base/'seven-content';content.mkdir()
        with zipfile.ZipFile(one) as z:z.extractall(content)
        seven=base/'seven.lilpck'
        subprocess.run(['7z','a','-t7z','-m0=lzma2','-md=1m',str(seven),'.'],cwd=content,check=True,stdout=subprocess.DEVNULL)
        call('install',seven);call('uninstall','Test App')
    if shutil.which('zstd'):
        tar=package('zstd-tar',fmt='tar');zst=base/'zstd.lilpck'
        subprocess.run(['zstd','-q',str(tar),'-o',str(zst)],check=True)
        call('install',zst);call('uninstall','Test App')
    for label,extra in [('two-scripts',{'duo.sh':b'x'}),('traversal',{'../outside':b'x'}),('absolute',{'/outside':b'x'}),
                        ('collision',{'DATA/x':b'x','data/X':b'y'}),('file-dir',{'item':b'x','item/child':b'y'})]:
        pack=package(label,extra=extra);call('inspect',pack,base/'badpreview',ok=False);assert not list(root.iterdir())
    for name in ['../escape','Bad:Name','CON','Trailing.','bad\x00name']:
        pack=package('badname'+str(count),name=name);call('inspect',pack,base/'badpreview',ok=False)
    wrapper=base/'wrapper.lilpck'
    with zipfile.ZipFile(wrapper,'w') as out:
        out.writestr('folder/meta.json',json.dumps({'name':'Wrapped','version':'1.0'}));out.writestr('folder/main.sh','x')
    call('inspect',wrapper,base/'badpreview',ok=False)
    no_launcher=base/'none.lilpck'
    with zipfile.ZipFile(no_launcher,'w') as out:out.writestr('meta.json','{"name":"None","version":"1"}')
    call('inspect',no_launcher,base/'badpreview',ok=False)
    linked=base/'linked.lilpck'
    with tarfile.open(linked,'w') as out:
        item=tarfile.TarInfo('link');item.type=tarfile.SYMTYPE;item.linkname='../outside';out.addfile(item)
    call('inspect',linked,base/'badpreview',ok=False)
    # A preview must not silently install a different file at the same path.
    call('install',one,'0'*64,ok=False);assert not list(root.iterdir())
    call('install',one);call('rename','Test App','../escape',ok=False)
    assert (root/'Test App/meta.json').exists();call('uninstall','Test App')
print(f'PASS {count} package lifecycle/format/validation operations ({"MIPS" if args.qemu else "host"}); physical storage untouched.')
