"""Actual short-file EOF/restart and oversized local text-input regressions."""
import os
from pathlib import Path
import re
import subprocess
import sys
import json

P=Path(__file__).resolve().parents[2]
out=Path(sys.argv[2] if len(sys.argv)>2 else '/root/lilsmoke-media-end-qa');out.mkdir(exist_ok=False)
subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:duration=0.5',str(out/'Short.wav')],check=True)
(out/'End.m3u').write_text('Short.wav\n')
(out/'restart.txt').write_text('1.2 pause\n')
env=os.environ|{'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy','LILSMOKE_MEDIA_QA':'1',
    'LILSMOKE_UI_HIDDEN':'1','LILSMOKE_UI_WINDOWED':'1','LILSMOKE_INPUT_DIR':str(out/'no-input'),
    'LILSMOKE_MEDIA_FONT':'/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
env.pop('LILSMOKE_M8_AUDIO',None)
with (out/'restart.log').open('w') as log:
    subprocess.run([sys.argv[1],str(out/'End.m3u'),'--qa-seconds','2','--qa-actions',str(out/'restart.txt')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
text=(out/'restart.log').read_text()
assert 'MEDIA_ACTION id=0 paused=0' in text,text
assert int(re.search(r'audio_frames=(\d+)',text)[1])>35000,text
(out/'Huge.m3u').write_bytes(b'\n'*262145)
(out/'Huge.srt').write_bytes(b'\n'*262145)
cc=['g++','-std=c++17','-O2','-I'+str(P/'package/lilsmoke-media/src'),'-x','c++','-','-o',str(out/'limits')]
source='#include "MediaPaths.h"\n#include "Captions.h"\n#include <cassert>\nint main(int argc,char**argv){assert(lilsmoke::media::collection(argv[1]).empty());assert(lilsmoke::media::loadCaptions(argv[2]).empty());}\n'
subprocess.run(cc,input=source,text=True,check=True)
subprocess.run([str(out/'limits'),str(out/'Huge.m3u'),str(out/'Huge.srt')],check=True,timeout=2)
(out/'report.json').write_text(json.dumps(dict(passed=True,production_eof_restart=True,bounded_oversized_playlists_and_captions=True,sanitizers=True),indent=2)+'\n')
print('PASS real EOF restart/audio replay and bounded oversized text inputs')
