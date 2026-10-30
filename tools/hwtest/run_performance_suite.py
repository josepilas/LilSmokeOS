#!/usr/bin/env python3
"""Sequential matched video/input comparisons; no capture or host build load."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--rom', required=True, type=Path)
    p.add_argument('--qemu', required=True)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--repeats', type=int, default=2)
    a = p.parse_args()
    if not 2 <= a.repeats <= 5: p.error('repeat count must be 2..5')
    a.out.mkdir(parents=True, exist_ok=False)
    artifacts = ROOT/'test-results/emulation-performance'
    cases = ['software-alsa', 'software-alsa-input', 'software-alsa-ui']
    rows = []
    for repeat in range(1, a.repeats + 1):
        # Alternate order to reduce a persistent order/host-load bias.
        variants = ['baseline', 'candidate'] if repeat % 2 else ['candidate', 'baseline']
        for variant in variants:
            out = a.out/f'{variant}-{repeat}'
            ra = 'retroarch-readback' if variant == 'baseline' else 'retroarch-candidate'
            sdl = 'libSDL2-baseline-safe.so' if variant == 'baseline' else 'libSDL2-candidate.so'
            command = [sys.executable, str(ROOT/'tools/hwtest/emulation_benchmark.py'),
                '--qemu', a.qemu, '--cpu', '74Kf-lilsmoke', '--rom', str(a.rom),
                '--core', str(artifacts/'pinned-cores/snes9x_2005_libretro.so'),
                '--retroarch', str(artifacts/ra), '--sdl', str(artifacts/sdl),
                '--directfb-input', str(artifacts/'libdirectfb_linux_input-abi.so'),
                '--ram', '128', '--frames', '600', '--verify',
                '--cases', ','.join(cases), '--out', str(out)]
            print(f'COMPARISON_BEGIN variant={variant} repeat={repeat}', flush=True)
            subprocess.run(command, check=True)
            for row in json.loads((out/'results.json').read_text()):
                hashes = row['core_hash']
                if hashes['video_hash'] != 'b11bb1c2' or hashes['audio_hash'] != 'b725c285':
                    raise RuntimeError(f'boot/title correctness mismatch: {out}/{row["name"]}')
                if int(row['core_profile']['rendered']) != 600 or int(row['core_profile']['duplicates']) != 0:
                    raise RuntimeError('workload skipped or duplicated video frames')
                rows.append(dict(variant=variant, repeat=repeat, path=str(out), **row))
    summary = []
    for case in cases:
        item = dict(case=case)
        for variant in ['baseline', 'candidate']:
            samples = [r['process_fps'] for r in rows if r['name'] == case and r['variant'] == variant]
            item[variant] = dict(samples=samples, median=statistics.median(samples))
        item['change_percent'] = (item['candidate']['median']/item['baseline']['median']-1)*100
        summary.append(item)
    result = dict(samples=rows, summary=summary,
        notice='600 boot/title frames; verify overhead included. No recording. One MIPS vCPU, 128 MiB, software DirectFB, ALSA. TCG throughput is not physical-console FPS.')
    (a.out/'comparison.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(summary, indent=2), flush=True)
    print('PERFORMANCE_COMPARISON_OK', flush=True)

if __name__ == '__main__': main()
