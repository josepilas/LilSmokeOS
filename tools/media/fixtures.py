"""Generate tiny legal synthetic media for real decoder/playback regression tests."""
from pathlib import Path
import subprocess
import sys
from PIL import Image,ImageDraw

out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def ff(name,args):
    target=out/name
    if target.exists():return
    subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error',*args,str(target)],check=True)
audio=['-f','lavfi','-i','sine=frequency=440:sample_rate=44100:duration=12','-metadata','title=Signal One','-metadata','artist=LilSmoke Test','-metadata','album=Native Media']
for name,codec in [('One.wav','pcm_s16le'),('Two.mp3','libmp3lame'),('Three.flac','flac'),('Four.ogg','libvorbis'),('Five.opus','libopus'),('Six.m4a','aac'),('Seven.wma','wmav2')]:ff(name,[*audio,'-c:a',codec])
for name,codec in [('Clip.mp4','libx264'),('Clip.avi','mpeg4'),('Clip.webm','libvpx'),('Clip.mkv','mjpeg')]:
    ff(name,['-f','lavfi','-i','testsrc2=size=320x180:rate=15:duration=12','-f','lavfi','-i','sine=frequency=660:sample_rate=44100:duration=12','-c:v',codec,'-threads','1','-c:a','libopus' if name.endswith('.webm') else 'aac','-shortest'])
image=Image.new('RGB',(640,360),'white');draw=ImageDraw.Draw(image)
colors=['#e53b47','#39a2df','#39bd76','#ffce51','#765ac5','#111a1f']
for i,c in enumerate(colors):draw.rectangle((i*640//6,0,(i+1)*640//6,280),fill=c)
draw.text((25,300),'LilSmoke photo decoder test  640 x 360',fill='black')
for name in ('Photo.png','Photo.jpg','Photo.webp','Photo.bmp','Photo.tiff','Photo.gif'):
    if not (out/name).exists():image.save(out/name)
(out/'Local.m3u').write_text('#EXTM3U\nOne.wav\nTwo.mp3\nhttps://invalid.example/song.mp3\nThree.flac\n')
(out/'Local.PLS').write_text('[playlist]\nFile1=One.wav\nFile2=Two.mp3\nNumberOfEntries=2\n')
(out/'Clip.srt').write_text('1\n00:00:00,000 --> 00:00:02,000\nNative software playback\n\n2\n00:00:03,000 --> 00:00:06,000\nAudio and video test\n')
(out/'actions-audio.txt').write_text('.4 pause\n.8 pause\n1.1 seek-forward\n1.5 seek-back\n1.9 shuffle\n2.2 next\n2.6 previous\n3 repeat\n3.3 mute\n3.6 volume-up\n4 playlist\n4.3 options\n')
(out/'actions-video.txt').write_text('.4 pause\n.8 pause\n1.1 seek-forward\n1.5 seek-back\n2.2 aspect\n2.8 subtitles\n3.2 audio\n3.8 options\n')
(out/'actions-photo.txt').write_text('.4 zoom-in\n.8 zoom-in\n1.2 rotate\n1.6 zoom-out\n2 fit\n2.4 next\n3 previous\n3.6 slide\n4 info\n')
(out/'Broken.mp4').write_bytes(b'invalid media, expected friendly error')
print('Fixtures ready:',out)
