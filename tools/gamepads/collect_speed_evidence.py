"""Keep diagnostic evidence, never private binaries, ROMs or extracted game assets."""
import json
from pathlib import Path
import re
import shutil

P = Path(__file__).resolve().parents[2]
out = P / 'test-results/sm64-speed-gamepad-boot-2026-10-11'
usb = max(Path('/root').glob('usb-pad-*/report.json'), key=lambda file: file.stat().st_mtime).parent
sources = {
    'sm64-optimized-app': Path('/root/sm64-fast-app-optimized-retry-20261011'),
    'usb-driver': usb,
    'renderer-sanitizers': Path('/root/sm64-raster-asan-final-20261011'),
    'sm64-baseline': Path('/root/sm64-profile-before-20261011'),
}
for name, source in sources.items():
    assert json.loads((source / 'report.json').read_text())['passed']
    destination = out / name
    destination.mkdir(parents=True, exist_ok=True)
    for file in source.rglob('*'):
        if file.is_file() and (file.name in ('report.json', 'memory.txt', 'serial.log', 'engine.log',
                'driver.log', 'probe.log', 'renderer.log', 'process.log') or file.suffix == '.png'):
            target = destination / file.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)

baseline = (sources['sm64-baseline'] / 'engine.log').read_text()
optimized = (sources['sm64-optimized-app'] / 'engine.log').read_text()
pattern = r'OriginalFrame=(\d+) (Level=.*)'
before = {int(frame): state for frame, state in re.findall(pattern, baseline) if int(frame) < 3000}
after = {int(frame): state for frame, state in re.findall(pattern, optimized) if int(frame) < 3000}
assert before and before == after, 'Original game progression changed at the same frame numbers'
def timing(text):
    groups = re.findall(r'SM64_PROFILE frames=(\d+).*?work_ms=([\d.]+) raster_ms=([\d.]+)', text)
    total = used = 0
    work = raster = 0.0
    for frames, milliseconds, drawing in groups:
        count = int(frames)
        if total + count > 3000: break
        total += count; used += count
        work += count * float(milliseconds); raster += count * float(drawing)
    return dict(frames=used, mean_work_ms=work/used, mean_raster_ms=raster/used)
report = dict(passed=True, original_progression_equal_through_frame_2970=True,
    baseline=timing(baseline), optimized=timing(optimized),
    reference_width=320, optimized_width=160, optimized_pixels_ratio=0.25,
    benchmark_platform='QEMU TCG Malta, one MIPS CPU, 120 MiB; not physical HC16xx speed',
    physical_console_fps_measured=False, physical_dongles_tested=False, audible_hardware_audio_tested=False)
(out / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
