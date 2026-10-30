"""Add a verified public PC utility to a generated regular .img, never a disk."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
from build_stock_candidate import BOARD, sha
import subprocess
from pc_tools_payload import verify_delivery, copy_to_fat
from stock_format import inspect

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image',type=Path,required=True)
parser.add_argument('--delivery',type=Path,required=True)
parser.add_argument('--resume-before',type=Path,
                    help='resume readback using retained, verified pre-copy boot/runtime captures')
args = parser.parse_args()

def run(command):
    result = subprocess.run(list(map(str,command)),stdin=subprocess.DEVNULL,
        capture_output=True,text=True,timeout=120)
    if result.returncode: raise RuntimeError(result.stderr)
    return result.stdout
image = args.image
if image.is_symlink() or not image.is_file() or image.suffix != '.img':
    parser.error('Only a regular generated .img is accepted')
report_path = image.with_suffix('.img.json')
report = json.loads(report_path.read_text())
if not args.resume_before and sha(image) != report['image_sha256']:
    parser.error('Generated image hash does not match its build report')
layout = inspect(image)
if [row['label'] for row in layout['partitions']] != ['LILOS','LILROMS']:
    parser.error('Only the two-partition LilSmoke image is accepted')
root,delivery = verify_delivery(args.delivery)
fat = f"{image}@@{layout['partitions'][0]['offset_bytes']}"
with tempfile.TemporaryDirectory(prefix='pc-tool-readback-') as temporary:
    temporary = Path(temporary)
    names = ('cubegm/vmlinux.uImage','cubegm/dtb.bin','cubegm/icube','lilsmoke/runtime.tsv')
    protected = {}
    for index,name in enumerate(names):
        target = args.resume_before/str(index) if args.resume_before else temporary/str(index)
        if not args.resume_before: run(['mcopy','-i',fat,'::/'+name,target])
        protected[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    if args.resume_before:
        expected = {'cubegm/vmlinux.uImage':report['stock_hashes']['vmlinux.uImage'],
            'cubegm/dtb.bin':report['stock_hashes']['dtb.bin'],
            'cubegm/icube':sha(BOARD/'icube-entry.sh'),
            'lilsmoke/runtime.tsv':sha(Path('build/m8-payload-candidate32/runtime.tsv'))}
        if protected != expected: raise RuntimeError('Unverified readback recovery captures')
        report['pc_tools'] = dict(files=delivery['file_count'],bytes=delivery['bytes'],
            proprietary_firmware_included=False,location='LILOS/PC-Tools/LilSmoke Device Adapter')
    else:
        report['pc_tools'] = copy_to_fat(root,fat)
    for index,(name,row) in enumerate(delivery['files'].items()):
        target = temporary/('tool-readback-'+str(index))
        run(['mcopy','-n','-i',fat,'::/'+name,target])
        if sha(target) != row['sha256']:
            raise RuntimeError('PC-tool image readback mismatch: '+name)
    for index,(name,expected) in enumerate(protected.items()):
        target = temporary/('protected-readback-'+str(index))
        run(['mcopy','-n','-i',fat,'::/'+name,target])
        if sha(target) != expected: raise RuntimeError('Image boot/runtime changed')
if inspect(image)['partitions'] != layout['partitions']:
    raise RuntimeError('Generated image geometry changed')
report['pc_tools'].update(readback_verified=True,protected_boot_runtime_files=protected)
report['image_sha256'] = sha(image)
report_path.write_text(json.dumps(report,indent=2)+'\n')
image.with_suffix('.img.sha256').write_text(report['image_sha256']+'  '+image.name+'\n')
print('PASS PC tool embedded and reread; boot/runtime/geometry unchanged')
