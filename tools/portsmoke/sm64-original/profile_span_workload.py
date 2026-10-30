#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Count real original-engine fused-path work; sampled rendering, NOT timing."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from test_span_div import budget, SDK


def instrument(text):
    starts = list(re.finditer(r'static void (opt_scan_\w+)\(', text))
    assert len(starts) == 12
    names = [m[1] for m in starts]
    result = text
    for i in range(11, -1, -1):
        start = starts[i].start()
        end = starts[i+1].start() if i < 11 else text.index('/* --- parameterized triangle walker')
        body = text[start:end]
        marker = '        const int cnt = (n > OPT_STEP) ? OPT_STEP : n;'
        assert body.count(marker) == 1 and body.count('            dst += cnt;') == 1
        body = body.replace(marker, marker + f'\n        ++path_blocks[{i}]; path_pixels[{i}] += cnt;')
        body = body.replace('            dst += cnt;', f'            ++path_hidden[{i}]; path_hidden_pixels[{i}] += cnt;\n            dst += cnt;')
        result = result[:start] + body + result[end:]
    globals = 'static unsigned long long path_blocks[12], path_pixels[12], path_hidden[12], path_hidden_pixels[12];\n'
    globals += '__attribute__((destructor)) static void report_paths(void) {\n'
    for i, name in enumerate(names):
        globals += f'    fprintf(stderr, "SPAN_PATH name={name} blocks=%llu pixels=%llu hidden_blocks=%llu hidden_pixels=%llu\\n", path_blocks[{i}], path_pixels[{i}], path_hidden[{i}], path_hidden_pixels[{i}]);\n'
    globals += '}\n'
    at = result.index('static inline bool opt_span_visible(')
    return result[:at] + globals + result[at:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    engine = args.engine.resolve()
    build = json.loads((engine / 'build.json').read_text())
    renderer = out / 'PRIVATE-profile-paths.c'
    renderer.write_text(instrument((engine / 'PRIVATE-gfx-soft-after.c').read_text()))
    obj, binary = out / 'PRIVATE-profile-paths.o', out / 'PRIVATE-profile-game'
    command = json.loads((engine / 'after-compile-command.json').read_text())
    old_obj = command[command.index('-o')+1]
    command[command.index('-c')+1] = str(renderer)
    command[command.index('-o')+1] = str(obj)
    link = json.loads((engine / 'after-link-command.json').read_text())
    link[link.index('-o')+1] = str(binary)
    link[link.index(old_obj)] = str(obj)
    for name, argv in (('compile', command), ('link', link)):
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(argv, cwd=build['source'], stdout=log, stderr=subprocess.STDOUT,
                preexec_fn=budget, check=True, timeout=600)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('SM64_', 'LILSMOKE_', 'QEMU_'))}
    env.update(HOME=str(out), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
        SM64_ORIGINAL_TEST_FRAMES='2700', SM64_ORIGINAL_RENDER_EVERY='15', SM64_ORIGINAL_PREFLIGHT='1', SM64_SOFTWARE_WIDTH='256')
    command = ['qemu-mipsel', '-cpu', '74Kf', '-R', '100663296', '-L', str(SDK / 'staging'),
        '-E', 'LD_LIBRARY_PATH=' + str(args.runtime / 'lib'), str(binary)]
    log = subprocess.check_output(command, cwd=out, env=env, text=True, stderr=subprocess.STDOUT,
        preexec_fn=budget, timeout=600)
    (out / 'engine.log').write_text(log)
    rendered = sum(frame % 15 == 0 or frame + 60 >= 2700 for frame in range(2700))
    assert f'Frames=2700 Rendered={rendered}' in log and 'Level=16 Area=1' in log
    paths = [dict(name=name, **dict(zip(('blocks', 'pixels', 'hidden_blocks', 'hidden_pixels'), map(int, values))))
        for name, *values in re.findall(r'SPAN_PATH name=(\w+) blocks=(\d+) pixels=(\d+) hidden_blocks=(\d+) hidden_pixels=(\d+)', log)]
    assert len(paths) == 12 and sum(p['hidden_pixels'] for p in paths) > 0
    paths.sort(key=lambda p: p['pixels'], reverse=True)
    report = dict(passed=True, actual_original_engine=True, simulated_frames=2700, rendered_frames=rendered,
        rendering_stride=15, final_60_frames_fully_rendered=True, performance_timing=False, paths=paths)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    binary.unlink()


if __name__ == '__main__':
    main()
