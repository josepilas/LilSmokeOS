"""Private 0.4/0.5 original-engine differential and guarded-detail regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from PIL import Image, ImageChops, ImageDraw

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--baseline',type=Path,required=True)
args = parser.parse_args()
args.out.mkdir(exist_ok=False)
build = json.loads((HERE/'_build/host-build.json').read_text())
binary = Path(build['privateBinary'])
assert 'LILSMOKE_SANITIZE=1' in build['command']
assert hashlib.sha256(binary.read_bytes()).hexdigest() == build['binarySha256']
cases = []
for width in (192,256,320):
    for smooth in (False,True):
        options = {'SM64_SOFTWARE_WIDTH':str(width),'SM64_WORLD_SCALE':'smooth' if smooth else 'nearest'}
        for version,executable in (('040',args.baseline),('050',binary)):
            cases.append((f'{version}-{width}-{options["SM64_WORLD_SCALE"]}',executable,options))
cases += [('detail-default',binary,{}),('detail-smooth',binary,{'SM64_WORLD_SCALE':'smooth'}),
    ('detail-override',binary,{'SM64_WORLD_QUALITY':'detail','SM64_SOFTWARE_WIDTH':'256'}),
    ('cycle',binary,{'SM64_ORIGINAL_TEST_WIDTH_CYCLE':'1'}),
    ('cycle-smooth',binary,{'SM64_ORIGINAL_TEST_WIDTH_CYCLE':'1','SM64_WORLD_SCALE':'smooth'})]
clean = {key:value for key,value in os.environ.items() if not key.startswith(('SM64_','LILSMOKE_'))}
reports = []
reference_state = reference_audio = None
for name,executable,options in cases:
    run = args.out/name
    run.mkdir()
    env = dict(clean,HOME=str(run),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
        SM64_ORIGINAL_TEST_FRAMES='3600',SM64_ORIGINAL_RENDER_EVERY='1',
        SM64_ORIGINAL_EVIDENCE=str(run),ASAN_OPTIONS='detect_leaks=0',
        UBSAN_OPTIONS='halt_on_error=1',OMP_NUM_THREADS='1',**options)
    start = time.monotonic()
    with (run/'engine.log').open('w') as output:
        subprocess.run([str(executable)],cwd=run,env=env,stdout=output,
            stderr=subprocess.STDOUT,check=True,timeout=300)
    elapsed = time.monotonic()-start
    text = (run/'engine.log').read_text()
    assert 'Frames=3600 Rendered=3600' in text and 'Level=16 Area=1' in text,name
    assert 'runtime error:' not in text and 'ERROR: AddressSanitizer' not in text,name
    state = re.findall(r'^OriginalFrame=.*$',text,re.M)
    audio = re.search(r'AudioBlocks=(\d+) AudioNonzero=(\d+) AudioHash=(\w+)',text).groups()
    if reference_state is None: reference_state,reference_audio = state,audio
    assert state == reference_state and audio == reference_audio,name
    assert int(audio[1]) > 0
    if name == 'detail-override': assert 'quality=fixed' in text
    if name.startswith('detail-') and name != 'detail-override':
        assert 'quality=detail' in text and 'world_bytes=153600' in text
        assert not re.search(r'SM64_QUALITY.*world=(160|192)x',text)
    for ppm in run.glob('frame-*.ppm'):
        im = Image.open(ppm)
        assert im.size == (320,240) and len(im.getcolors(320*240)) > 4
        im.save(ppm.with_suffix('.png'))
    reports.append(dict(case=name,passed=True,fully_rendered_frames=3600,
        original_state_and_audio_unchanged=True,sanitizer_faults=0,host_elapsed_seconds=elapsed))
    print('PASS original-engine',name,round(elapsed,2),flush=True)
for width in (192,256,320):
    for scale in ('nearest','smooth'):
        for file in (args.out/f'040-{width}-{scale}').glob('frame-*.ppm'):
            before = Image.open(file)
            after = Image.open(args.out/f'050-{width}-{scale}'/file.name)
            assert ImageChops.difference(before,after).getbbox() is None,(width,scale,file.name)
comparison = Image.new('RGB',(960,264),'white')
draw = ImageDraw.Draw(comparison)
for index,(case,label) in enumerate((('040-256-nearest','0.4: 256x192'),
        ('050-320-nearest','Current: 320x240 detail ceiling'),('050-256-smooth','Current: optional smooth 256x192'))):
    draw.text((index*320+8,5),label,fill='black')
    comparison.paste(Image.open(args.out/case/'frame-2100.ppm'),(index*320,24))
comparison.save(args.out/'comparison.png')
report = dict(passed=True,current_version=json.loads((HERE.parents[2]/'package/lilsmoke-sm64-original/local-app/meta.json').read_text())['version'],
    fully_rendered_frames=3600*len(cases),cases=reports,
    same_size_pixels_identical_to_040=True,all_resize_transitions_exercised=True,
    original_state_and_audio_identical_to_040=True,physical_console_fps_measured=False,
    binary_sha256=build['binarySha256'])
(args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
