#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Three alternating full-render 320 pairs using guest PROCESS CPU time.

Only the private QA adapter's timing counters change, never production code.
The ordinary raster counter still excludes presentation/upscaling. This avoids
attributing nice/scheduling delays to the renderer while other builds run.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

from test_span_div import budget
from test_performance_engine import execute, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    engine, out = args.engine.resolve(), args.out.resolve()
    out.mkdir(exist_ok=False)
    builds = json.loads((engine / 'build.json').read_text())
    text = (engine / 'PRIVATE-headless-platform.c').read_text()
    marker = 'extern void game_exit(void);'
    timing = '''#include <time.h>
static uint64_t qa_process_cpu_counter(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value)) { perror("QA process clock"); exit(2); }
    return (uint64_t)value.tv_sec * 1000000000ULL + value.tv_nsec;
}
#define SDL_GetPerformanceCounter qa_process_cpu_counter
#define SDL_GetPerformanceFrequency() 1000000000ULL

'''
    assert text.count(marker) == 1
    platform = out / 'PRIVATE-cpu-clock-platform.c'
    platform.write_text(text.replace(marker, timing + marker, 1))
    obj = out / 'PRIVATE-cpu-clock-platform.o'
    command = json.loads((engine / 'before-compile-command.json').read_text())
    command[command.index('-c')+1] = str(platform)
    command[command.index('-o')+1] = str(obj)
    command = [arg for arg in command if arg not in ('-O3', '-fno-math-errno', '-mno-shared', '-mplt')]
    command += ['-Ililsmoke']
    (out / 'compile-command.json').write_text(json.dumps(command, indent=2)+'\n')
    with (out / 'compile.log').open('w') as log:
        subprocess.run(command, cwd=builds['source'], stdout=log, stderr=subprocess.STDOUT,
            preexec_fn=budget, check=True, timeout=600)
    products = {}
    for version in ('before', 'after'):
        binary = out / ('PRIVATE-game-' + version)
        link = json.loads((engine / f'{version}-link-command.json').read_text())
        link[link.index('-o')+1] = str(binary)
        link = [str(obj) if arg.endswith('/PRIVATE-headless-platform.o') else arg for arg in link]
        (out / f'{version}-link-command.json').write_text(json.dumps(link, indent=2)+'\n')
        with (out / f'{version}-link.log').open('w') as log:
            subprocess.run(link, cwd=builds['source'], stdout=log, stderr=subprocess.STDOUT,
                preexec_fn=budget, check=True, timeout=600)
        products[version] = dict(binary=binary.name, sha256=sha(binary))
    results = []
    for sample in range(3):
        for version in (('before', 'after') if sample % 2 == 0 else ('after', 'before')):
            result = execute(out, products, args.runtime, f'320-{sample}-{version}', version,
                {'SM64_SOFTWARE_WIDTH': '320'}, 2700)
            results.append(result)
            # Preserve each completed run even if a later test is interrupted.
            (out / f'320-{sample}-{version}/result.json').write_text(json.dumps(result, indent=2)+'\n')
    pairs = []
    original = json.loads((engine / 'report.json').read_text())
    for sample in range(3):
        before = next(r for r in results if r['name'] == f'320-{sample}-before')
        after = next(r for r in results if r['name'] == f'320-{sample}-after')
        for field in ('state', 'audio', 'images'):
            assert before[field] == after[field], (sample, field)
            prior = next(r for r in original['runs'] if r['name'] == '320-before')
            assert before[field] == prior[field], ('CPU-clock QA changes game output', field)
        pairs.append(dict(sample=sample, before_raster_cpu_ms=before['raster_ms'],
            after_raster_cpu_ms=after['raster_ms'], after_over_before=after['raster_ms']/before['raster_ms']))
    ratio = statistics.median(pair['after_over_before'] for pair in pairs)
    report = dict(passed=True, fully_rendered_frames=16200, alternating_pairs=3,
        width=320, hud_width=320, timing_clock='guest CLOCK_PROCESS_CPUTIME_ID, QEMU user process',
        presentation_excluded=True, pixel_state_audio_equal=True, same_output_as_wall_clock_qa=True,
        no_production_clock_changes=True, median_paired_after_over_before=ratio,
        no_median_cpu_regression=ratio <= 1, physical_fps_measured=False, pairs=pairs, runs=results)
    (out / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('320 CPU raster ratio', ratio, 'no regression:', ratio <= 1, flush=True)


if __name__ == '__main__':
    main()
