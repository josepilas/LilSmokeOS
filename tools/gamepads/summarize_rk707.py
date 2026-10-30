import json
from pathlib import Path
P=Path(__file__).resolve().parents[2]
data=json.loads((P/'test-results/sm64-readable-rk707-2026-10-11/rk707-ps-hid.json').read_text())
last=None
for item in data['reports']:
    b=item['bytes'];state=tuple(b[:3])
    if state!=last:
        print(f"{item['seconds']:6.2f}s bytes={len(b)} buttons={int.from_bytes(bytes(b[:2]),'little'):04x} hat={b[2]:02x} axes={b[3:7]}")
        last=state
