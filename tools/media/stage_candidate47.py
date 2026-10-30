"""Stage a qualified two-object media update; never opens a card."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

P=Path(__file__).resolve().parents[2]
BASELINE='e3d9f9d007f2db28477ff7abc56bec82db0d09f9ee28e3f237e7505707626323'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--qualification',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args()
    q=json.loads((a.qualification/'report.json').read_text())
    required={'reproducible-build','native-protocol','headless-controls','ram-canvas','worker-protocol','video-recovery','worker-linux','host','video','audio'}
    assert q['passed'] and q['candidate']==47 and set(q['gates'])==required
    assert all(g['passed'] for g in q['gates'].values())
    assert sha(a.binary)==q['binary_sha256']==sha(a.qualification/'rebuild/lilsmoke-media')
    assert sha(a.runtime/'hcplayer-worker')==q['worker_sha256']
    assert sha(a.runtime/'manifest.json')==q['runtime_manifest_sha256']
    for rel,digest in q['source_sha256'].items():assert sha(P/rel)==digest,rel
    previous=json.loads((P/'build/media-candidate45-20261024/payload.json').read_text())
    baseline={Path(row['target']).name:row for row in previous['files'] if '/hcplayer/' in row['target']}
    runtime=json.loads((a.runtime/'manifest.json').read_text())
    assert runtime['profile']=='HC16xx-video-664-audio-disabled'
    for row in runtime['files']:
        assert sha(a.runtime/row['file'])==row['staged_sha256']
        if row['file']!='hcplayer-worker':assert row['staged_sha256']==baseline[row['file']]['sha256']
    a.out.mkdir(parents=True,exist_ok=False)
    files=[]
    for name,target,source in [('000001.bin','usr/bin/lilsmoke-media',a.binary),('000002.bin','usr/lib/lilsmoke/hcplayer/hcplayer-worker',a.runtime/'hcplayer-worker')]:
        shutil.copyfile(source,a.out/name)
        assert sha(a.out/name)==sha(source)
        files.append(dict(object=name,target=target,sha256=sha(source),bytes=source.stat().st_size,mode='0755'))
    shutil.copyfile(a.qualification/'report.json',a.out/'qualification-report.json')
    for gate in required-{'reproducible-build'}:
        report=a.qualification/gate/'report.json'
        assert json.loads(report.read_text())['passed']
        shutil.copyfile(report,a.out/(gate+'-report.json'))
    (a.out/'payload.json').write_text(json.dumps(dict(candidate=47,expected_manifest=BASELINE,files=files,
        qualification_sha256=sha(a.out/'qualification-report.json'),
        baseline_runtime=list(baseline.values()),physical_m8_video_confirmed=False,partition_changes=False),indent=2)+'\n')
    print('PASS staged qualified Candidate47:',a.out)

if __name__=='__main__':main()
