#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Exercise the actual MIPS FFmpeg CLI with generated, non-game media."""
import argparse
import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import wave


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,default=Path('/root/lilsmoke-os/build/buildroot/output'))
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    out=args.out.resolve()
    out.mkdir(exist_ok=False)
    programs={name:args.sdk/'target/usr/bin'/name for name in ('ffmpeg','ffprobe')}
    for path in programs.values():
        with path.open('rb') as binary: header=binary.read(20)
        assert header[:6]==b'\x7fELF\x01\x01' and int.from_bytes(header[18:20],'little')==8
    commands=[]
    def run(name,arguments,check=True):
        command=['qemu-mipsel','-cpu','74Kf','-L',str(args.sdk/'staging'),'-E',
            'LD_LIBRARY_PATH='+str(args.sdk/'target/usr/lib')+':'+str(args.sdk/'staging/lib'),
            str(programs[name]),*map(str,arguments)]
        began=time.monotonic()
        result=subprocess.run(command,capture_output=True,timeout=180)
        commands.append(dict(program=name,arguments=list(map(str,arguments)),exit=result.returncode,
            seconds=round(time.monotonic()-began,3)))
        if check and result.returncode:
            raise RuntimeError(result.stderr.decode(errors='replace')[-4000:])
        return result
    common=['-nostdin','-hide_banner','-loglevel','error','-threads','1','-filter_threads','1']
    version=run('ffmpeg',['-version']).stdout.decode()
    assert 'ffmpeg version 6.1.6' in version
    protocols=run('ffmpeg',['-hide_banner','-protocols']).stdout.decode().splitlines()
    assert 'file' in [line.strip() for line in protocols] and 'pipe' in [line.strip() for line in protocols]
    assert not any(line.strip() in ('http','https','tcp','udp') for line in protocols)
    pcm=array.array('h',(int(10000*math.sin(2*math.pi*440*t/16000)) for t in range(16000)))
    if sys.byteorder!='little': pcm.byteswap()
    raw=pcm.tobytes()
    source=out/'signal.wav'
    with wave.open(str(source),'wb') as target:
        target.setnchannels(1);target.setsampwidth(2);target.setframerate(16000);target.writeframes(raw)
    flac=out/'signal.flac'
    run('ffmpeg',common+['-i',source,'-c:a','flac',flac])
    probe=json.loads(run('ffprobe',['-v','error','-show_streams','-of','json',flac]).stdout)
    assert probe['streams'][0]['codec_name']=='flac' and probe['streams'][0]['sample_rate']=='16000'
    decoded=run('ffmpeg',common+['-i',flac,'-f','s16le','-c:a','pcm_s16le','pipe:1']).stdout
    assert decoded==raw
    filtered=out/'filtered.wav'
    run('ffmpeg',common+['-i',source,'-af','volume=0.5,aresample=22050','-c:a','pcm_s16le',filtered])
    with wave.open(str(filtered),'rb') as target:
        assert target.getframerate()==22050 and target.getnchannels()==1
        samples=array.array('h',target.readframes(target.getnframes()))
        if sys.byteorder!='little': samples.byteswap()
        assert 4800<=max(map(abs,samples))<=5200
    width,height,frames=64,48,8
    pixels=bytes(component for frame in range(frames) for y in range(height) for x in range(width)
        for component in ((x*3+frame*7)%256,(y*4+frame*5)%256,((x+y)*2+frame*3)%256))
    rawvideo=out/'pattern.rgb'
    rawvideo.write_bytes(pixels)
    movie=out/'pattern.mp4'
    run('ffmpeg',common+['-f','rawvideo','-pixel_format','rgb24','-video_size','64x48',
        '-framerate','10','-i',rawvideo,'-frames:v',str(frames),'-an','-c:v','mpeg4','-q:v','3',movie])
    video=json.loads(run('ffprobe',['-v','error','-show_streams','-of','json',movie]).stdout)['streams'][0]
    assert (video['width'],video['height'],int(video['nb_frames']))==(width,height,frames)
    rgb=run('ffmpeg',common+['-i',movie,'-pix_fmt','rgb24','-f','rawvideo','pipe:1']).stdout
    assert len(rgb)==len(pixels)
    mse=sum((a-b)**2 for a,b in zip(pixels,rgb))/len(pixels)
    psnr=99 if not mse else 10*math.log10(255*255/mse)
    assert psnr>20
    broken=out/'broken.mp4'
    broken.write_bytes(b'generated invalid media')
    assert run('ffprobe',['-v','error',broken],check=False).returncode>0
    result=dict(passed=True,architecture='MIPS32r2 little-endian musl',physical_device=False,
        cli=True,probe=True,audio_lossless_roundtrip=True,software_filters=True,
        video_encode_decode=True,video_psnr_db=round(psnr,2),corrupt_input_rejected=True,
        local_file_and_pipe_only=True,pcm_sha256=hashlib.sha256(raw).hexdigest(),commands=commands)
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:value for key,value in result.items() if key!='commands'},indent=2))


if __name__=='__main__': main()
