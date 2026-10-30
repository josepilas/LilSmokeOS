"""Read only the user's RK707 PS-mode HID reports; never send output/keys."""
import argparse
import json
from pathlib import Path
import sys
import time
P=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(P/'build/windows-hid-probe'))
import hid
parser=argparse.ArgumentParser()
parser.add_argument('--seconds',type=int,default=10)
args=parser.parse_args()
devices=[d for d in hid.enumerate(0x0079,0x181c) if d['usage_page']==1 and d['usage'] in (4,5)]
assert len(devices)==1, 'Expected one RK707 PS-mode gamepad; no keyboard/mouse opened'
device=hid.device();device.open_path(devices[0]['path']);device.set_nonblocking(1)
records=[];last=None;deadline=time.monotonic()+args.seconds
try:
    while time.monotonic()<deadline:
        report=device.read(128)
        if report and report!=last:
            records.append(dict(seconds=round(time.monotonic()-deadline+args.seconds,3),bytes=report))
            last=report
        time.sleep(.005)
finally:device.close()
info={k:v for k,v in devices[0].items() if k!='path'}
out=P/'test-results/sm64-readable-rk707-2026-10-11/rk707-ps-hid.json'
out.write_text(json.dumps(dict(read_only=True,device=info,reports=records),indent=2)+'\n')
print(json.dumps(dict(device=info,unique_reports=len(records),samples=records[:5]),indent=2))
