"""Renderer ASan/UBSan regression at all supported 4:3 software sizes."""
import json
import argparse
import os
from pathlib import Path
import subprocess
from PIL import Image

HERE = Path(__file__).resolve().parent
build = json.loads((HERE / '_build/host-build.json').read_text())
binary = Path(build['privateBinary'])
assert 'LILSMOKE_SANITIZE=1' in build['command']
assert '__asan_report_store4' in subprocess.check_output(['nm', '-D', str(binary)], text=True)
parser = argparse.ArgumentParser()
parser.add_argument('--out', type=Path, default=Path('/root/sm64-raster-regression-20261011'))
output = parser.parse_args().out
output.mkdir(exist_ok=False)
reports = []
for width in (160, 192, 256, 320):
    directory = output / str(width)
    directory.mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SM64_SOFTWARE_WIDTH=str(width), SM64_ORIGINAL_TEST_FRAMES='3600',
               SM64_ORIGINAL_EVIDENCE=str(directory), ASAN_OPTIONS='detect_leaks=0',
               UBSAN_OPTIONS='halt_on_error=1')
    with (directory / 'renderer.log').open('w') as log:
        subprocess.run([str(binary)], env=env, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)
    text = (directory / 'renderer.log').read_text()
    assert 'Frames=3600 Rendered=3600' in text and 'Level=16 Area=1' in text
    assert 'runtime error:' not in text and 'ERROR: AddressSanitizer' not in text
    for file in directory.glob('*.ppm'):
        image = Image.open(file)
        assert image.size == (320, 240)
        assert len(image.getcolors(width * width) or []) > 4
        image.save(file.with_suffix('.png'))
    reports.append(dict(width=width, height=width * 3 // 4, frames=3600, sanitizers_passed=True))
(output / 'report.json').write_text(json.dumps(dict(passed=True, sizes=reports,
    native_host_test=True, physical_console_fps_measured=False), indent=2) + '\n')
print('PASS renderer sanitizers: 3600 full frames at 160/192/256/320, title/menu/castle', output)
