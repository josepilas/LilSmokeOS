#!/usr/bin/env python3
"""Private original-ROM interaction and decoder cross-check; never packages data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import wave


def reference_sound(rom, bank, instrument):
    ctl = rom[5748512:5748512+97856]
    tbl = rom[5846368:5846368+2216704]
    offset, length = struct.unpack_from('>II', ctl, 4+bank*8)
    body = ctl[offset+16:offset+length]
    inst, = struct.unpack_from('>I', body, 4+instrument*4)
    sample, tuning = struct.unpack_from('>If', body, inst+16)
    zero, start, loop, book_at, count = struct.unpack_from('>IIIII', body, sample)
    assert zero == 0
    order, predictors = struct.unpack_from('>II', body, book_at)
    assert order == 2
    book = struct.unpack_from('>'+str(order*8*predictors)+'h', body, book_at+8)
    _, frames = struct.unpack_from('>II', body, loop)
    base, _ = struct.unpack_from('>II', tbl, 4+bank*8)
    data = tbl[base+start:base+start+count]
    assert count%9 in (0,1)
    data = data[:count//9*9]
    assert frames <= len(data)//9*16+1
    frames = min(frames,len(data)//9*16)
    state = [0]*16
    output = []
    for at in range(0, len(data), 9):
        scale, predictor = 1 << (data[at] >> 4), data[at] & 15
        residual = []
        for byte in data[at+1:at+9]:
            for value in (byte >> 4, byte & 15):
                residual.append((value if value < 8 else value-16)*scale)
        # Expanded predictor vectors, independently matching aifc_decode.c.
        coefficients = []
        for row in range(8):
            coefficients.append([book[predictor*16+row], book[predictor*16+8+row]]+
                                [book[predictor*16+8+row-j-1] for j in range(row)])
        for group in range(2):
            history = state[14:16] if group == 0 else state[6:8]
            for row in range(8):
                state[group*8+row] = sum(a*b for a,b in zip(coefficients[row], history+residual[group*8:group*8+row]))//2048+residual[group*8+row]
                output.append(max(-32768, min(32767, state[group*8+row])))
    pcm = struct.pack('<'+'h'*frames, *output[:frames])
    return dict(frames=frames, rate=int(32000*tuning), pcm_sha1=hashlib.sha1(pcm).hexdigest()), pcm


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--sdk',type=Path,required=True)
    args=parser.parse_args()
    rom=args.rom.read_bytes();before=hashlib.sha256(rom).hexdigest()
    assert hashlib.sha1(rom).hexdigest()=='9bef1128717f958171a4afac3ed78ee2bb4e86ce'
    expected={}
    for bank,instrument in ((8,2),(0,4),(8,10)):
        metadata,pcm=reference_sound(rom,bank,instrument)
        expected[(bank,instrument)]=metadata
        if (bank,instrument)==(8,2):
            with wave.open(str(args.root/'tests/mario-jump.wav'),'wb') as file:
                file.setnchannels(1);file.setsampwidth(2);file.setframerate(metadata['rate']);file.writeframes(pcm)
    results={}
    for target in ('host','mips','asan'):
        with tempfile.TemporaryDirectory(prefix='sm64-progress-private-',dir='/root') as directory:
            private=Path(directory);original=private/'original';original.mkdir();cache=private/'.sm64';cache.mkdir()
            shutil.copyfile(args.rom,original/'any user ROM name')
            command=[str(args.root/'tests'/target),str(original),str(cache)]
            env=dict(os.environ,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
            if target=='mips':
                command=['qemu-mipsel','-cpu','74Kf','-L',str(args.sdk/'staging'),*command]
                env['LD_LIBRARY_PATH']=str(args.sdk/'staging/usr/lib')+':'+str(args.sdk/'staging/lib')
            run=subprocess.run(command,env=env,capture_output=True,text=True,timeout=240)
            log=run.stdout+run.stderr;(args.root/'tests'/f'{target}.log').write_text(log)
            assert run.returncode==0,log
            assert 'AddressSanitizer' not in log and 'runtime error:' not in log,log
            sounds={}
            for match in re.finditer(r'Sound bank=(\d+) instrument=(\d+) frames=(\d+) rate=(\d+) pcm_sha1=(\w+)',run.stdout):
                key=(int(match[1]),int(match[2]))
                actual=dict(frames=int(match[3]),rate=int(match[4]),pcm_sha1=match[5])
                assert actual==expected[key],(target,key,actual,expected[key])
                sounds[str(key)]=actual
            assert len(sounds)==3 and 'CourseProgressChecks=' in log and 'PASS' in log
            assert not list(original.iterdir()),'only private test app ROM should be removed'
            results[target]=dict(passed=True,sounds=sounds)
    assert hashlib.sha256(args.rom.read_bytes()).hexdigest()==before
    result=dict(passed=True,checks='ROM macro objects/meshes, eight red coins, star spawning/collection/save/reload, damage/invulnerability/stomp, original VADPCM against independent reference, SDL audio open/play/pause/close',
                physical_m8_tested=False,background_music=False,more_maps=False,targets=results)
    (args.root/'tests/result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
