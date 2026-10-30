#!/usr/bin/env python3
"""Disclosed cuts from the final uncut recording, always at original speed."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path, help='exported final-vt-validation folder')
    args = parser.parse_args()
    source = args.folder/'lilsmoke-snes-ui.webm'
    spans = [(0,20),(28,35),(50,65),(80,90),(197,211),
             (285,300),(346,364),(379,396),(411,420),(460,470)]
    target = args.folder/'ui-highlights.webm'
    # A split/trim/concat filter graph can retain large future-frame queues.
    # Encode one bounded segment at a time, then remux the compatible streams.
    with tempfile.TemporaryDirectory(prefix='.highlights-', dir=args.folder) as work:
        directory = Path(work)
        files = []
        for i,(start,end) in enumerate(spans):
            segment = directory/f'segment-{i}.webm'
            subprocess.run(['ffmpeg','-y','-nostdin','-hide_banner','-loglevel','error',
                            '-ss',str(start),'-i',str(source),'-t',str(end-start),
                            '-map','0:v:0','-an','-c:v','libvpx-vp9','-deadline','good',
                            '-cpu-used','6','-threads','1','-crf','30','-b:v','0',
                            '-pix_fmt','yuv420p',str(segment)],check=True)
            files.append(f"file '{segment.name}'\n")
            print(f'HIGHLIGHT_SEGMENT_OK {i+1}/{len(spans)}',flush=True)
        listing = directory/'concat.txt'
        listing.write_text(''.join(files))
        subprocess.run(['ffmpeg','-y','-nostdin','-hide_banner','-loglevel','error',
                        '-f','concat','-safe','1','-i',str(listing),'-map','0:v:0',
                        '-c','copy',str(target)],check=True)
    edits = dict(source=source.name, output=target.name,
                 source_intervals_seconds=spans, playback_speed=1.0,
                 notice='Edited highlights, not continuous footage. Idle time and some transitions omitted. No footage sped up; original uncut recording retained.')
    (args.folder/'highlight-edits.json').write_text(json.dumps(edits,indent=2))
    probe = subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format',
                                     '-of','json',str(target)],text=True)
    (args.folder/'highlight-probe.json').write_text(probe)
    print(f'NORMAL_SPEED_HIGHLIGHTS_CREATED {target}',flush=True)

if __name__ == '__main__':
    main()
