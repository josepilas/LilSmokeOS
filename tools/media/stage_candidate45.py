"""Prepare a small verified card payload; no card writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

P=Path(__file__).resolve().parents[2]
def sha(file):return hashlib.sha256(file.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    evidence={}
    for label,path in {
        'host':Path('/root/lilsmoke-media-final-host-20261024/report.json'),
        'video':Path('/root/lilsmoke-media-c45-final-video/report.json'),
        'audio':Path('/root/lilsmoke-media-c45-final-audio/report.json'),
        'pacing':Path('/root/lilsmoke-media-pacing-20261024-final/report.json'),
        'native_protocol':Path('/root/lilsmoke-native-protocol-20261024/report.json'),
        'native_abi':Path('/root/lilsmoke-hcplayer-release-vm-20261024/report.json')}.items():
        report=json.loads(path.read_text());assert report['passed'],label
        shutil.copyfile(path,a.out/(label+'-report.json'));evidence[label]=report
    native=json.loads((a.runtime/'manifest.json').read_text())
    entries=[('usr/bin/lilsmoke-media',a.binary),
        ('usr/lib/lilsmoke/lilsmoke-media-open',P/'package/lilsmoke-media/lilsmoke-media-open')]
    for row in native['files']:
        file=a.runtime/row['file'];assert sha(file)==row['staged_sha256']
        entries.append(('usr/lib/lilsmoke/hcplayer/'+file.name,file))
    files=[]
    for index,(target,source) in enumerate(entries,1):
        name=f'{index:06}.bin';shutil.copyfile(source,a.out/name)
        files.append(dict(target=target,object=name,sha256=sha(a.out/name),mode='0755'))
    (a.out/'payload.json').write_text(json.dumps(dict(candidate=45,files=files,
        expected_manifest='af1ce3dd1f52031cf0eff1bb0b608fc079d21da175b46768a338c890f2739099',
        physical_decoder_tested=False,partition_changes=False),indent=2)+'\n')
    print('PASS staged candidate45 payload:',len(files),'objects',a.out)

if __name__=='__main__':main()
