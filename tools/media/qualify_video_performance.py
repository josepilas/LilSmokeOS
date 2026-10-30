"""CPU1 serial video qualification, reusing the SDK/rootfs; no deployment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import resource
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]
SDK = Path('/root/lilsmoke-os/build/buildroot/output')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--performance', required=True, type=Path)
    a = parser.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    os.sched_setaffinity(0, {1})
    os.nice(10)
    os.environ['LILSMOKE_MEDIA_BUILD_CPU'] = '1'
    paths = [*P.joinpath('package/lilsmoke-media/src').glob('*'),
             *P.joinpath('package/lilsmoke-ui/src/src').glob('*.cpp'),
             *P.joinpath('package/lilsmoke-ui/src/src').glob('*.h'),
             P/'package/opendingux-emulators/m8_pocketsnes_audio.c',
             P/'board/segam-m8-v6-stock/m8-audio.c']
    snapshot = {str(p.relative_to(P)): digest(p) for p in paths if p.is_file()}
    gates = {}

    def run(label, command, timeout=900, env=None):
        print('RUN', label, flush=True)
        start = time.monotonic()
        (a.out/'running.json').write_text(json.dumps(dict(stage=label, command=list(map(str, command)))))
        with (a.out/(label+'.log')).open('w') as log:
            subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=timeout, env=env)
        gates[label] = dict(passed=True, seconds=round(time.monotonic()-start, 3))

    run('optimized-profile', [sys.executable, P/'tools/media/test_video_performance.py',
                             '--out', a.performance, '--stage', 'optimized', '--owner-source',
                             P/'build/media-video-performance-20261027/private-input'])
    old = json.loads((a.performance/'baseline/report.json').read_text())
    new = json.loads((a.performance/'optimized/report.json').read_text())
    assert old['inputs'] == new['inputs']
    paired = {}
    for target in ('host', 'mips'):
        paired[target] = {}
        execute = [] if target=='host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', str(SDK/'staging')]
        for file in old[target]['samples']:
            input_file = next(Path(p) for p in old['inputs'] if Path(p).name==file)
            samples = dict(baseline=[], optimized=[])
            for trial in range(3):
                for stage in (('baseline', 'optimized') if trial%2==0 else ('optimized', 'baseline')):
                    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
                    result = subprocess.run([*execute, str(a.performance/stage/('profile-'+target)), str(input_file)],
                                            capture_output=True, text=True, check=True, timeout=240)
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    sample = json.loads(result.stdout)
                    sample['process_cpu_ms'] = (after.ru_utime+after.ru_stime-usage.ru_utime-usage.ru_stime)*1000
                    samples[stage].append(sample)
            paired[target][file] = samples
    (a.out/'paired-samples.json').write_text(json.dumps(paired, indent=2)+'\n')
    comparisons = {}
    for target in ('host', 'mips'):
        comparisons[target] = {}
        for file, samples in paired[target].items():
            before, after = samples['baseline'], samples['optimized']
            for result in before + after:
                for key in ('frames', 'pcm_frames', 'pcm_hash', 'pts_hash', 'last_pts'):
                    assert result[key] == before[0][key], (target, file, key)
            b, n = statistics.median(x['total_ms'] for x in before), statistics.median(x['total_ms'] for x in after)
            comparisons[target][file] = dict(baseline_ms=b, optimized_ms=n, speedup=b/n,
                cpu_speedup=statistics.median(x['process_cpu_ms'] for x in before)/statistics.median(x['process_cpu_ms'] for x in after),
                baseline_fps=statistics.median(x['fps'] for x in before),
                optimized_fps=statistics.median(x['fps'] for x in after),
                all_frames_and_pts_retained=True, pcm_bit_identical=True)
    (a.out/'comparison.json').write_text(json.dumps(comparisons, indent=2)+'\n')
    for target in ('host', 'mips'):
        compiler = 'g++' if target=='host' else SDK/'host/bin/mipsel-buildroot-linux-musl-g++'
        binary = a.out/('overlay-'+target)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if target=='host' else []
        run('build-overlay-'+target, [compiler, '-O2', '-std=c++17', *flags,
             '-I'+str(P/'package/lilsmoke-media/src'), P/'tools/media/test_overlay_blit.cpp', '-o', binary])
        execute = [] if target=='host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', SDK/'staging']
        run('overlay-'+target, [*execute, binary])
    for label, script in [('video-recovery', 'test_video_recovery.py'),
                          ('native-protocol', 'test_native_protocol.py')]:
        run(label, [sys.executable, P/'tools/media'/script, '--out', a.out/label])
    run('pcm-regression', [sys.executable, P/'tools/media/test_recovery.py', '--out', a.out/'pcm-regression',
        '--media', a.performance/'private/owner.mp4', a.performance/'private/owner.webm'])
    run('build-host-asan', ['bash', P/'tools/media/build_host.sh', a.out/'host'],
        env=os.environ|{'LILSMOKE_MEDIA_SANITIZE':'1'})
    run('host-controls-formats', [sys.executable, P/'tools/media/host_playback.py',
        a.out/'host/lilsmoke-media', a.out/'host-controls-formats'])
    run('build-mips', ['bash', P/'tools/media/build_mips.sh', a.out/'mips'])
    run('rebuild-mips', ['bash', P/'tools/media/build_mips.sh', a.out/'rebuild'])
    binary = a.out/'mips/lilsmoke-media'
    assert digest(binary) == digest(a.out/'rebuild/lilsmoke-media')
    for target in ('host', 'mips'):
        run('build-sync-'+target, ['bash', P/('tools/media/build_host.sh' if target=='host' else 'tools/media/build_mips.sh'),
            a.out/('sync-'+target)], env=os.environ|{'LILSMOKE_MEDIA_MAIN':str(P/'tools/media/test_video_sync.cpp'),
                                                   'LILSMOKE_MEDIA_SANITIZE':'1'})
        execute = [] if target=='host' else ['qemu-mipsel', '-cpu', '74Kf', '-L', SDK/'staging']
        for file in ('owner.mp4', 'owner.webm'):
            run('sync-'+target+'-'+file, [*execute, a.out/('sync-'+target)/'lilsmoke-media', a.performance/'private'/file],
                env=os.environ|{'ASAN_OPTIONS':'detect_leaks=0', 'UBSAN_OPTIONS':'halt_on_error=1'})
    for label, file in [('vm-video', 'Clip.mp4'), ('vm-audio', 'One.mp3')]:
        run(label, [sys.executable, P/'tools/media/mips_explorer_vm.py', a.out/label, file,
             '--binary', binary, '--ui', P/'build/media-recovery-20261024/card-before/lilsmoke-ui',
             '--fixtures', P/'build/media-recovery-20261024/PRIVATE-vm-media',
             '--rootfs', '/root/lilsmoke-media-c45-final-video/rootfs.squashfs'])
        assert json.loads((a.out/label/'report.json').read_text())['binary_sha256'] == digest(binary)
    assert {str(p.relative_to(P)): digest(p) for p in paths if p.is_file()} == snapshot
    report = dict(passed=True, binary_sha256=digest(binary), host_sha256=digest(a.out/'host/lilsmoke-media'),
                  source_sha256=snapshot, gates=gates, comparisons=comparisons,
                  cpu_affinity=[1], hardware_m8_timing_measured=False, sd_writes=False,
                  firmware_worker_changed=False, shared_audio_sources_changed=False,
                  no_new_frame_skip_policy=True)
    (a.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS', digest(binary), flush=True)


if __name__ == '__main__':
    main()
