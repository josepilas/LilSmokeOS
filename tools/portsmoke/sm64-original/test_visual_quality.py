"""Private full-frame quality regression; never infer physical FPS from host/QEMU."""
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
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--baseline', type=Path, required=True, help='preserved 0.3.0 host binary')
parser.add_argument('--verify-only', action='store_true', help='recheck completed runs without executing the engine again')
args = parser.parse_args()
args.out = args.out.resolve()
args.baseline = args.baseline.resolve()
args.out.mkdir(exist_ok=args.verify_only)
build = json.loads((HERE / '_build/host-build.json').read_text())
binary = Path(build['privateBinary'])
assert 'LILSMOKE_SANITIZE=1' in build['command']
assert hashlib.sha256(binary.read_bytes()).hexdigest() == build['binarySha256']

cases = [
    ('baseline192', args.baseline, {'SM64_SOFTWARE_WIDTH': '192'}),
    ('default256', binary, {}),
    *[(f'fixed{width}', binary, {'SM64_SOFTWARE_WIDTH': str(width)}) for width in (160, 192, 256, 320)],
    ('performance', binary, {'SM64_WORLD_QUALITY': 'performance'}),
    ('quality', binary, {'SM64_WORLD_QUALITY': 'quality'}),
    ('adaptive', binary, {'SM64_WORLD_QUALITY': 'adaptive'}),
    ('width_priority', binary, {'SM64_WORLD_QUALITY': 'adaptive', 'SM64_SOFTWARE_WIDTH': '192'}),
    ('smooth192', binary, {'SM64_SOFTWARE_WIDTH': '192', 'SM64_WORLD_SCALE': 'smooth'}),
    ('smooth256', binary, {'SM64_WORLD_SCALE': 'smooth'}),
    ('smooth320', binary, {'SM64_SOFTWARE_WIDTH': '320', 'SM64_WORLD_SCALE': 'smooth'}),
    ('cycle', binary, {'SM64_ORIGINAL_TEST_WIDTH_CYCLE': '1'}),
    ('cycle_smooth', binary, {'SM64_ORIGINAL_TEST_WIDTH_CYCLE': '1', 'SM64_WORLD_SCALE': 'smooth'}),
    ('texture_filter', binary, {'SM64_WORLD_FILTER': '1'}),
]
logs, reports = {}, []
clean = {key: value for key, value in os.environ.items()
         if not key.startswith(('SM64_', 'LILSMOKE_'))}
for name, executable, options in cases:
    run = args.out / name
    run.mkdir(exist_ok=args.verify_only)
    env = dict(clean, HOME=str(run), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
        SM64_ORIGINAL_TEST_FRAMES='3600', SM64_ORIGINAL_RENDER_EVERY='1',
        SM64_ORIGINAL_EVIDENCE=str(run), ASAN_OPTIONS='detect_leaks=0',
        UBSAN_OPTIONS='halt_on_error=1', OMP_NUM_THREADS='1', **options)
    if name != 'baseline192':
        env['SM64_ORIGINAL_TEST_WORLD_EVIDENCE'] = '1'
    start = time.monotonic()
    if not args.verify_only:
        with (run / 'engine.log').open('w') as output:
            subprocess.run([str(executable)], cwd=run, env=env, stdout=output,
                stderr=subprocess.STDOUT, check=True, timeout=300)
    log = (run / 'engine.log').read_text()
    assert 'Frames=3600 Rendered=3600' in log, name
    assert 'Level=16 Area=1' in log, name
    assert 'runtime error:' not in log and 'ERROR: AddressSanitizer' not in log, name
    state = re.findall(r'^OriginalFrame=.*$', log, re.M)
    audio = re.search(r'AudioBlocks=(\d+) AudioNonzero=(\d+) AudioHash=(\w+)', log).groups()
    if name == 'baseline192':
        baseline_state, baseline_audio = state, audio
    else:
        assert state == baseline_state, f'{name}: game-state/camera/timer/input regression'
        assert audio == baseline_audio, f'{name}: audio synthesis regression'
    assert int(audio[1]) > 0
    for file in run.glob('*.ppm'):
        im = Image.open(file)
        assert im.size == (320, 240)
        if file.name.startswith('frame-'):
            assert len(im.getcolors(320 * 240)) > 4
        im.save(file.with_suffix('.png'))
    assert len(list(run.glob('frame-*.ppm'))) == 10, name
    logs[name] = log
    reports.append(dict(case=name, frames=3600, rendered=3600, sanitizer_faults=0,
        original_state_matches_030=True, original_audio_matches_030=True,
        reused_completed_run=args.verify_only,
        elapsed_host_seconds=None if args.verify_only else round(time.monotonic() - start, 3)))
    print('PASS full-engine', name, flush=True)

def frame(case, number=2100, kind='frame'):
    return Image.open(args.out / case / f'{kind}-{number:04d}.ppm').convert('RGB')

def identical(a, b):
    for number in (45, 240, 390, 510, 1200, 1440, 2100, 2300, 2700, 3599):
        assert ImageChops.difference(frame(a, number), frame(b, number)).getbbox() is None, (a, b, number)

identical('baseline192', 'fixed192')
identical('fixed192', 'performance')
identical('fixed192', 'width_priority')
identical('default256', 'fixed256')
identical('fixed320', 'quality')
identical('fixed320', 'smooth320')
assert 'world=256x192 hud=320x240' in logs['default256']
assert 'quality=fixed' in logs['width_priority']
for width in (160, 192, 256, 320):
    assert f'world={width}x{width * 3 // 4}' in logs['cycle']
    assert f'world={width}x{width * 3 // 4}' in logs['cycle_smooth']
assert ImageChops.difference(frame('fixed192'), frame('fixed256')).getbbox()
assert ImageChops.difference(frame('fixed256'), frame('smooth256')).getbbox()

# Mask opaque HUD/text fragments using pre-overlay evidence, not world colors.
reference, before = frame('fixed192'), frame('fixed192', kind='world')
hud_mask, text_mask = [], []
for y in range(8, 33):
    for x in list(range(40, 85)) + list(range(218, 260)):
        color = reference.getpixel((x, y))
        if color != before.getpixel((x, y)) and (color == (255, 255, 255) or
                (color[0] >= 248 and color[1] >= 100 and color[2] <= 66)):
            hud_mask.append((x, y))
for y in range(40, 132):
    for x in range(45, 174):
        color = reference.getpixel((x, y))
        if color in ((255, 255, 255), (255, 251, 255)) and before.getpixel((x, y)) != color:
            text_mask.append((x, y))
assert len(hud_mask) >= 100 and len(text_mask) >= 100, (len(hud_mask), len(text_mask))
for name, _, _ in cases:
    image = frame(name)
    assert all(image.getpixel(p) == reference.getpixel(p) for p in hud_mask), (name, 'HUD')
    assert all(image.getpixel(p) == reference.getpixel(p) for p in text_mask), (name, 'dialog text')

comparison = Image.new('RGB', (960, 264), 'white')
draw = ImageDraw.Draw(comparison)
for i, (name, label) in enumerate((('fixed192', '0.3.0 world 192x144'),
                                  ('fixed256', '0.4.0 default world 256x192'),
                                  ('smooth256', '0.4.0 optional smooth 256x192'))):
    draw.text((i * 320 + 8, 5), label, fill='black')
    comparison.paste(frame(name), (i * 320, 24))
comparison.save(args.out / 'comparison-2100.png')

# Strict configuration errors must fail early; production does not enable QA stride/cycle.
for key, value in (('SM64_SOFTWARE_WIDTH', '240'), ('SM64_WORLD_QUALITY', 'fast'),
                   ('SM64_WORLD_SCALE', 'blur'), ('SM64_WORLD_FILTER', '2')):
    run = args.out / ('invalid-' + key)
    run.mkdir(exist_ok=args.verify_only)
    result = subprocess.run([str(binary)], cwd=run,
        env=dict(clean, SDL_VIDEODRIVER='dummy', **{key: value}), capture_output=True, timeout=15)
    assert result.returncode == 2, (key, result.returncode)

report = dict(passed=True, cases=reports, fully_rendered_frames=3600 * len(cases),
    hud_opaque_pixels_identical=len(hud_mask), dialog_opaque_pixels_identical=len(text_mask),
    nearest_192_identical_to_030=True, full_resolution_scaler_identity=True,
    width_override_priority=True, all_resize_transitions_exercised=True,
    binary_sha256=build['binarySha256'], evidence_private_not_distributable=True,
    physical_console_fps_measured=False, notice='Host sanitizer/state tests, not HC16xx timing.')
(args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS quality: original state/audio, full frames, unchanged HUD/text, configurations/resize/scaling')
