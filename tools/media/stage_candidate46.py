"""Stage tested media fixes for the newly cloned 32 GB card, no card writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
P=Path(__file__).resolve().parents[2]
EXPECTED='1772a8ffb6f77ca18f142a48ebd7343b3eea0dc01889f42dd7079f27afaef201'
QUALIFIED=Path('/root/lilsmoke-c46-final-qualification')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    binary=Path('/root/lilsmoke-media-c46-deploy-mips/lilsmoke-media')
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==EXPECTED
    assert binary.read_bytes()==(QUALIFIED/'rebuild/lilsmoke-media').read_bytes()
    qualification=json.loads((QUALIFIED/'report.json').read_text())
    assert qualification['passed'] and qualification['binary_sha256']==EXPECTED
    shutil.copyfile(QUALIFIED/'report.json',a.out/'qualification-report.json')
    reports={
        'recovery':Path('/root/lilsmoke-c46-recovery-all-transports/report.json'),
        'ram-canvas':QUALIFIED/'ram-canvas/report.json',
        'native-protocol':QUALIFIED/'native-protocol/report.json',
        'headless-controls':QUALIFIED/'headless-controls/report.json',
        'host':QUALIFIED/'host/report.json',
        'video':QUALIFIED/'video/report.json',
        'audio':QUALIFIED/'audio/report.json',
    }
    for label,file in reports.items():
        assert json.loads(file.read_text())['passed'],label
        shutil.copyfile(file,a.out/(label+'-report.json'))
    shutil.copyfile(binary,a.out/'000001.bin')
    (a.out/'payload.json').write_text(json.dumps(dict(candidate=46,sha256=EXPECTED,
        target='usr/bin/lilsmoke-media',object='000001.bin',
        expected_manifest='c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445',
        partition_changes=False,physical_m8_video_confirmed=False),indent=2)+'\n')
    print('PASS candidate46 payload:',a.out)
if __name__=='__main__':main()
