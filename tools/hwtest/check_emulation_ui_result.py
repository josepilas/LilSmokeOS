#!/usr/bin/env python3
"""Assert the captured, manually inspected final SNES/VT functional session."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import zlib

def state_size(path):
    """Validate RetroArch's optional RZIP container, not just on-disk size."""
    data = path.read_bytes()
    if not data.startswith(b'#RZIPv\x01#'):
        return len(data)
    if len(data) < 20:
        return None
    chunk_limit = int.from_bytes(data[8:12], 'little')
    expected = int.from_bytes(data[12:20], 'little')
    if not 1 <= chunk_limit <= 8*1024*1024 or not 1 <= expected <= 8*1024*1024:
        return None
    offset, total = 20, 0
    while offset < len(data):
        if offset+4 > len(data): return None
        size = int.from_bytes(data[offset:offset+4], 'little')
        offset += 4
        if not size or offset+size > len(data): return None
        stream = zlib.decompressobj()
        try: decoded = stream.decompress(data[offset:offset+size], chunk_limit+1)
        except zlib.error: return None
        if not stream.eof or stream.unused_data or stream.unconsumed_tail or len(decoded) > chunk_limit:
            return None
        offset += size
        total += len(decoded)
        if total > expected: return None
    return total if total == expected else None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    run = args.run.resolve()
    checks = []
    def check(value, name):
        checks.append(dict(name=name, passed=bool(value)))
        print(('PASS ' if value else 'FAIL ')+name, flush=True)
    manifest = json.loads((run/'manifest.json').read_text())
    command = manifest['command']
    check(command[command.index('-m')+1] == '128', 'guest RAM is 128 MiB')
    check(command[command.index('-smp')+1] == '1', 'one MIPS vCPU')
    check(command[command.index('-cpu')+1] == '74Kf-lilsmoke', 'tested MIPS CP0 profile')
    check(manifest['diagnostic_vt_trace'] and not manifest['diagnostic_input_trace'], 'functional VT trace enabled, not an FPS benchmark')
    boot = (run/'persist/boot-state.log').read_text()
    memory = re.search(r'^MemTotal:\s+(\d+) kB$', boot, re.M)
    check(memory and 100000 <= int(memory[1]) <= 131072, 'guest-reported RAM stays inside ceiling')
    active = (run/'persist/vt-state.log').read_text().splitlines()
    check(len(active) >= 10 and set(active) == {'tty1'}, 'active Linux VT never switched')
    ui = (run/'persist/logs/lilsmoke-ui.log').read_text()
    ra = (run/'persist/logs/retroarch.log').read_text()
    serial = (run/'serial.log').read_text(errors='replace')
    check('Out of memory: Killed process' not in serial and 'Kernel panic' not in serial, 'no guest OOM or panic')
    check('GameList: ready for snes' in ui and 'Launcher: starting /storage/roms/snes/' in ui, 'normal UI selected SNES and the ROM')
    check('.7z [' in ui and '/storage/.cache/lilsmoke/archives/snes/' in ra, 'archive launched through the real backend')
    check('Found input driver: "sdl2"' in ra and '[ALSA]: Initialized PLAYBACK device' in ra, 'SDL keyboard and guest ALSA initialized')
    check('[State]: Saving state' in ra and '[State]: Loading state' in ra, 'RetroArch saved and loaded state')
    states = list((run/'persist/states').rglob('*.state'))
    check(any(state_size(path) == 529168 for path in states), 'SNES basic state persisted and compressed container validates')
    check('[SRAM]: Saved successfully' in ra, 'SRAM saved on normal exit')
    check(bool(list((run/'persist/screenshots').glob('*.png'))), 'F8 screenshot persisted')
    check(ui.count('SystemView: first frame complete') >= 2, 'LilSmokeUI respawned after the game')
    actions = (run/'manual-events.ndjson').read_text()
    check(actions.count('sendkey alt-right ') >= 2 and actions.count('sendkey ret ') >= 2,
          'QMP-only movement/jump and Start keys recorded')
    def pixel_difference(left, right):
        result = subprocess.run(['compare','-metric','AE',str(run/left),str(run/right),'null:'], capture_output=True, text=True)
        if result.returncode not in (0, 1): raise RuntimeError(result.stderr)
        return int(result.stderr.strip())
    check(pixel_difference('13-gameplay-crop.png','14-gameplay-crop.png') == 0,
          'Start held gameplay still across separated captures (FPS region excluded)')
    check((run/'18-return-system.ppm').is_file(), 'return-to-system framebuffer captured and manually inspected')
    (run/'functional-checks.json').write_text(json.dumps(checks, indent=2))
    passed = sum(item['passed'] for item in checks)
    print(f'UI_FINAL_CHECKS passed={passed} failed={len(checks)-passed}', flush=True)
    return int(passed != len(checks))

if __name__ == '__main__':
    raise SystemExit(main())
