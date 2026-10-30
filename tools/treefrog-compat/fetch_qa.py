#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fetch the original Hex-a-hop QA fixture, not private game data or an SDK.

Git blob IDs are checked before publication. The original port is never patched.
Also supports a bounded remote-ZIP reader for the official runtime companion.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

HERE=Path(__file__).resolve().parent
REPO='https://api.github.com/repos/tzubertowski/TreeFrogUI_PortsCollection'
RAW='https://raw.githubusercontent.com/tzubertowski/TreeFrogUI_PortsCollection'


def request(url,limit=16*1024*1024):
    with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'LilSmoke-compat-QA'}),timeout=45) as r:
        data=r.read(limit+1)
    if len(data)>limit: raise ValueError('Download exceeds bounded fixture size')
    return data


class RemoteZip(io.RawIOBase):
    def __init__(self,url,size): self.url=url;self.size=size;self.pos=0
    def seekable(self):return True
    def readable(self):return True
    def tell(self):return self.pos
    def seek(self,offset,whence=0):
        value=offset+(self.pos if whence==1 else self.size if whence==2 else 0)
        if value<0 or value>self.size:raise ValueError('Invalid remote ZIP seek')
        self.pos=value;return value
    def read(self,n=-1):
        if n<0:n=self.size-self.pos
        n=min(n,self.size-self.pos)
        if not n:return b''
        if n>16*1024*1024:raise ValueError('Remote ZIP member exceeds budget')
        start=self.pos;end=start+n-1
        req=urllib.request.Request(self.url,headers={'Range':f'bytes={start}-{end}',
            'Accept-Encoding':'identity','User-Agent':'LilSmoke-compat-QA'})
        with urllib.request.urlopen(req,timeout=45) as r:
            if r.status!=206 or r.headers.get('Content-Range')!=f'bytes {start}-{end}/{self.size}':
                raise ValueError('Server ignored the bounded HTTP range')
            data=r.read(n+1)
        if len(data)!=n:raise ValueError('Truncated remote ZIP range')
        self.pos+=n;return data


def fixture(out,port='hexahop'):
    tree=json.loads(request(REPO+'/git/trees/main?recursive=1'))
    commit=tree['sha'];manifest=[];total=0
    for item in tree['tree']:
        name=item['path']
        if item['type']!='blob' or not name.startswith('ports/'+port+'/'):continue
        relative=Path(name).relative_to('ports/'+port)
        if item['size']>4*1024*1024:raise ValueError('Unexpected large fixture file')
        target=out/'roms/ports'/port/relative
        data=request(f'{RAW}/{commit}/{name}',4*1024*1024)
        blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if blob!=item['sha'] or len(data)!=item['size']:raise ValueError('Git blob mismatch: '+name)
        total+=len(data)
        if total>20*1024*1024:raise ValueError('Fixture exceeds total size budget')
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        if relative.name=='run.sh' or relative.suffix=='.mipsel':target.chmod(0o755)
        manifest.append(dict(path=name,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data)))
    report=dict(repository=REPO,commit=commit,files=manifest,bytes=total,game=port,
        private_owner_assets=False,modified_port_files=False)
    (out/'fixture.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(commit=commit,files=len(manifest),bytes=total)),flush=True)


def runtime(out):
    # Deliberately fetch only the frontend and its published notices, not the
    # 160-MiB UI/core collection. Never install its kernel or device drivers.
    release=json.loads(request('https://api.github.com/repos/tzubertowski/TreeFrogUI/releases/latest'))
    asset=next(x for x in release['assets'] if x['name'].startswith('TreeFrogUI_') and x['name'].endswith('.zip'))
    files=[]
    with zipfile.ZipFile(RemoteZip(asset['browser_download_url'],asset['size'])) as z:
        for member in z.infolist():
            p=Path(member.filename)
            skin='/cubegm/skin/' in p.as_posix() and not member.is_dir()
            if not (p.as_posix().endswith('cubegm/picoarch') or skin or p.name in ('LICENSE.md',)):continue
            if member.file_size>8*1024*1024:raise ValueError('Runtime entry exceeds size budget')
            name=('cubegm/skin/'+p.as_posix().split('/cubegm/skin/',1)[1]) if skin else 'cubegm/picoarch' if p.name=='picoarch' else 'UPSTREAM-LICENSE.md'
            target=out/name;target.parent.mkdir(parents=True,exist_ok=True)
            data=z.read(member);target.write_bytes(data)
            if p.name=='picoarch':target.chmod(0o755)
            files.append(dict(member=member.filename,path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    if not (out/'cubegm/picoarch').is_file():raise ValueError('Runtime missing picoarch')
    (out/'runtime.json').write_text(json.dumps(dict(tag=release['tag_name'],
        archive=asset['browser_download_url'],files=files,redistribution_qualified=False,
        note='Private interoperability fixture. Matching source closure must be audited before redistribution.'),indent=2)+'\n')
    print(json.dumps(files),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('kind',choices=['fixture','runtime']);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',choices=['hexahop','classicube','sdlzombies'],default='hexahop')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if a.kind=='fixture':fixture(a.out,a.port)
    else:runtime(a.out)
