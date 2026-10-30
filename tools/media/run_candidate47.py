"""Serial, restart-auditable rebuild and qualification of the final C47 sources."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

P=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    os.nice(10);os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
    state={'passed':False,'completed':[],'stage':None}
    def run(name,cmd,env=None,timeout=1800):
        state['stage']=name;(a.out/'state.json').write_text(json.dumps(state,indent=2)+'\n')
        print('RUN',name,flush=True)
        with (a.out/(name+'.log')).open('w') as log:
            subprocess.run(list(map(str,cmd)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=timeout)
        state['completed'].append(name)
    try:
        run('host',['bash',P/'tools/media/build_host.sh',a.out/'host'],os.environ|{'LILSMOKE_MEDIA_SANITIZE':'1'})
        run('mips',['bash',P/'tools/media/build_mips.sh',a.out/'mips'])
        run('worker',[sys.executable,'-B',P/'tools/media/stage_hc_worker.py','--out',a.out/'runtime'])
        run('qualification',[sys.executable,'-B',P/'tools/media/qualify_candidate46.py','--candidate','47',
            '--runtime',a.out/'runtime','--binary',a.out/'mips/lilsmoke-media','--host',a.out/'host/lilsmoke-media','--out',a.out/'qualification'])
        state.update(passed=True,stage='complete')
    finally:
        (a.out/'state.json').write_text(json.dumps(state,indent=2)+'\n')
    print('PASS final Candidate47 rebuild and qualification',flush=True)

if __name__=='__main__':main()
