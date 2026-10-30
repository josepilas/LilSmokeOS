"""Real FFmpeg + production SDL software player; synthetic files, not M8 FPS."""
import json
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

P=Path(__file__).resolve().parents[2]
binary=Path(sys.argv[1] if len(sys.argv)>1 else '/root/lilsmoke-media-host/lilsmoke-media')
fixtures=Path('/root/lilsmoke-media-fixtures')
out=Path(sys.argv[2] if len(sys.argv)>2 else '/root/lilsmoke-media-qa');out.mkdir(exist_ok=False)
env=os.environ|{'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy','LILSMOKE_MEDIA_QA':'1',
    'LILSMOKE_UI_HIDDEN':'1','LILSMOKE_UI_WINDOWED':'1','LILSMOKE_INPUT_DIR':str(out/'no-devices'),
    'LILSMOKE_MEDIA_FONT':'/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
env.pop('LILSMOKE_M8_AUDIO',None)
results=[]
for width,height in ((640,480),(320,240),(854,480)):
    for name,actions in [('One.wav','audio'),('Clip.mp4','video'),('Photo.webp','photo')]:
        directory=out/f'{width}-{name}';directory.mkdir()
        setting=env|{'LILSMOKE_SCREEN_WIDTH':str(width),'LILSMOKE_SCREEN_HEIGHT':str(height)}
        with (directory/'player.log').open('w') as log:
            subprocess.run([str(binary),str(fixtures/name),'--qa-seconds','5',
                '--qa-actions',str(fixtures/f'actions-{actions}.txt'),'--qa-captures',str(directory)],
                env=setting,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
        text=(directory/'player.log').read_text()
        assert 'MEDIA_DONE' in text and 'runtime error:' not in text and 'ERROR: AddressSanitizer' not in text
        if actions=='audio':assert re.search(r'audio_frames=[1-9]\d*',text)
        if actions!='audio':assert re.search(r'decoded=[1-9]\d*',text)
        images=list(directory.glob('*.bmp'));assert len(images)>=3
        for file in images:
            im=Image.open(file);assert im.size==(width,height);assert len(im.getcolors(width*height) or [])>5
            im.save(file.with_suffix('.png'))
        results.append({'file':name,'size':[width,height],'controls':actions,'passed':True})
for name in ['Two.mp3','Three.flac','Four.ogg','Five.opus','Six.m4a','Seven.wma',
             'Clip.avi','Clip.webm','Clip.mkv','Photo.png','Photo.jpg','Photo.bmp','Photo.tiff','Photo.gif','Local.m3u','Local.PLS']:
    with (out/(name+'.log')).open('w') as log:
        subprocess.run([str(binary),str(fixtures/name),'--qa-seconds','1'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
    text=(out/(name+'.log')).read_text();assert 'MEDIA_DONE' in text
    results.append({'file':name,'passed':True})
with (out/'invalid.log').open('w') as log:
    result=subprocess.run([str(binary),str(fixtures/'Broken.mp4'),'--qa-seconds','1'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=30)
assert result.returncode==1 and 'error=Open media:' in (out/'invalid.log').read_text()
(out/'report.json').write_text(json.dumps({'passed':True,'actual_production_player':True,
    'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
    'synthetic_media':True,'software_sdl':True,'physical_console_fps_measured':False,'results':results},indent=2)+'\n')
print('PASS real media playback/control/resize/decoder tests:',out)
