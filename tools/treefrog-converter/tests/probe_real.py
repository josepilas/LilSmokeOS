# SPDX-License-Identifier: MPL-2.0
"""Private real-core ABI probe. Never copies game data to the website."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--package',type=Path,required=True);ap.add_argument('--stock',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    a.out.mkdir(parents=True,exist_ok=False)
    report=dict(passed=False,physical_device=False,vm=False,memory_budget_qualified=False,private_game_data_published=False)
    with tempfile.TemporaryDirectory(prefix='lilsmoke-real-core-') as temp,zipfile.ZipFile(a.package) as z:
        root=Path(temp);adapter=json.loads(z.read('adapter.json'));meta=json.loads(z.read('meta.json'))
        names=['runtime/frontend/lilretro','runtime/launch.json', 'runtime/port/'+adapter['binary']]
        if adapter['content']:names.append('runtime/port/'+adapter['content'])
        names += [n for n in z.namelist() if n.startswith('runtime/lib/')]
        for name in names:
            dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True)
            with z.open(name) as source,dest.open('xb') as target:shutil.copyfileobj(source,target,1024*1024)
        runner=root/'runtime/frontend/lilretro';runner.chmod(0o700)
        command=['qemu-mipsel','-cpu','74Kf','-L',str(a.stock.resolve()),str(runner),str(root/'runtime/port'/adapter['binary'])]
        if adapter['content']:command.append(str(root/'runtime/port'/adapter['content']))
        command += ['--profile',str(root/'runtime/launch.json'),'--headless','--probe','--frames','180','--diagnostics',str(a.out.resolve()/'runtime-capabilities.json')]
        env=dict(os.environ,LD_LIBRARY_PATH=':'.join(str(p.resolve()) for p in (root/'runtime/lib',a.stock/'lib',a.stock/'usr/lib')))
        for key in ('LD_PRELOAD','TF_DRIVER','TF_PRESENT'):env.pop(key,None)
        report.update(application=meta['name'],core=adapter['binary'],frontend_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),command=command)
        try:
            result=subprocess.run(command,cwd=root/'runtime/port',env=env,capture_output=True,timeout=120)
            (a.out/'stdout.log').write_bytes(result.stdout);(a.out/'stderr.log').write_bytes(result.stderr);report['exit']=result.returncode
            capabilities=a.out/'runtime-capabilities.json'
            if capabilities.exists():report['runtime']=json.loads(capabilities.read_text())
            report['passed']=result.returncode==0 and bool(report.get('runtime',{}).get('ok'))
        except subprocess.TimeoutExpired as e:
            report['error']='120-second core probe timeout';(a.out/'stderr.log').write_bytes(e.stderr or b'')
        finally:(a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='runtime'},indent=2))
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
