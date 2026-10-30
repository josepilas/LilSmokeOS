"""Verify and finalize an interrupted update of the generated candidate only."""
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
P=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(P/'tools/m8'))
from build_stock_candidate import sha
from stock_format import inspect
image=(P/'build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img').resolve()
payload=P/'build/m8-payload-candidate16'
assert image.is_file() and image.parent==P.resolve()/'build'
assert [part['label'] for part in inspect(image)['partitions']]==['LILOS','LILROMS']
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as file:
        for data in iter(lambda:file.read(1024*1024),b''):h.update(data)
    return h.hexdigest()
with tempfile.TemporaryDirectory(prefix='lil-app-refresh-',dir='/root') as temp:
    mount=Path(temp)
    subprocess.run(['mount','-o','loop,ro,offset=1048576,sizelimit=536870912',str(image),str(mount)],check=True)
    try:
        for name in ['runtime.tsv','m8-launch.sh','libm8-display.so','candidate.json']:
            assert digest(mount/'lilsmoke'/name)==digest(payload/name),name
        count=0
        for file in (payload/'files').iterdir():
            assert digest(mount/'lilsmoke/files'/file.name)==digest(file),file.name
            count+=1
    finally:subprocess.run(['umount',str(mount)],check=True)
report_path=image.with_suffix('.img.json')
report=json.loads(report_path.read_text())
embedded=json.loads((payload/'candidate.json').read_text())
report.update(embedded);report['output']=str(image);report['image_sha256']=sha(image)
report_path.write_text(json.dumps(report,indent=2)+'\n')
image.with_suffix('.img.sha256').write_text(report['image_sha256']+'  '+image.name+'\n')
evidence=P/'test-results/lilsmoke-applications-2026-10-09/generated-image-verified.json'
evidence.write_text(json.dumps(dict(passed=True,objects_verified=count,image_sha256=report['image_sha256'],physical_media_opened=False),indent=2)+'\n')
print('PASS finalized generated image after full runtime readback:',report['image_sha256'])
