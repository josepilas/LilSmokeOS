"""Serial, restart-auditable C46 qualification; never opens a Windows SD volume."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

P = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    paths = [*Path(P/'package/lilsmoke-media').rglob('*'),
             *Path(P/'package/lilsmoke-ui/src/src').glob('*'),
             P/'package/opendingux-emulators/m8_pocketsnes_audio.c',
             P/'board/segam-m8-v6-stock/m8-audio.c']
    return {str(p.relative_to(P)): digest(p) for p in sorted(paths)
            if p.is_file() and p.suffix in ('.c', '.h', '.cpp')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--candidate',type=int,choices=(46,47),default=46)
    parser.add_argument('--runtime',type=Path,help='Pinned isolated HC worker runtime required for Candidate47')
    a = parser.parse_args()
    if a.candidate==47 and not a.runtime:
        parser.error('Candidate47 requires --runtime')
    a.out.mkdir(parents=True, exist_ok=False)
    snapshot = sources()
    expected = digest(a.binary)
    results = {}

    def run(label, command, timeout=600):
        start = time.monotonic()
        state = dict(stage=label, command=[str(x) for x in command], passed=False)
        (a.out/'running.json').write_text(json.dumps(state, indent=2)+'\n')
        print('RUN', label, flush=True)
        with (a.out/(label+'.log')).open('w') as log:
            subprocess.run([str(x) for x in command], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=timeout)
        results[label] = dict(passed=True, seconds=round(time.monotonic()-start, 2))

    run('reproducible-build', ['bash', P/'tools/media/build_mips.sh', a.out/'rebuild'])
    assert digest(a.out/'rebuild/lilsmoke-media') == expected, 'Non-reproducible MIPS executable'
    for label, script in [('native-protocol', 'test_native_protocol.py'),
                          ('headless-controls', 'test_headless_controls.py'),
                          ('ram-canvas', 'test_ram_canvas.py')]:
        run(label, [sys.executable, P/'tools/media'/script, '--out', a.out/label])
    if a.candidate==47:
        for label,script in [('worker-protocol','test_hc_worker_protocol.py'),('video-recovery','test_video_recovery.py')]:
            run(label,[sys.executable,P/'tools/media'/script,'--out',a.out/label],timeout=900)
        manifest=json.loads((a.runtime/'manifest.json').read_text())
        worker=next(f for f in manifest['files'] if f['file']=='hcplayer-worker')
        assert worker['source_sha256']==digest(P/'package/lilsmoke-media/src/hcplayer_worker.c')
        assert worker['staged_sha256']==digest(a.runtime/'hcplayer-worker')
        run('worker-linux',[sys.executable,P/'tools/media/test_hc_worker_vm.py','--runtime',a.runtime,'--out',a.out/'worker-linux'])
    run('host', [sys.executable, P/'tools/media/host_playback.py', a.host, a.out/'host'])
    for label, selected in [('video', 'Clip.mp4'), ('audio', 'One.mp3')]:
        run(label, [sys.executable, P/'tools/media/mips_explorer_vm.py', a.out/label, selected,
                    '--binary', a.binary,
                    '--ui', P/'build/media-recovery-20261024/card-before/lilsmoke-ui',
                    '--fixtures', P/'build/media-recovery-20261024/PRIVATE-vm-media',
                    '--rootfs', '/root/lilsmoke-media-c45-final-video/rootfs.squashfs'])
        assert json.loads((a.out/label/'report.json').read_text())['binary_sha256'] == expected
    assert sources() == snapshot, 'Production sources changed during qualification'
    report = dict(passed=True, candidate=a.candidate, binary_sha256=expected,
                  source_sha256=snapshot, gates=results, physical_m8_video_confirmed=False,
                  sd_card_accessed=False)
    if a.candidate==47:
        report['worker_sha256']=worker['staged_sha256']
        report['runtime_manifest_sha256']=digest(a.runtime/'manifest.json')
    (a.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (a.out/'running.json').unlink()
    print('PASS candidate'+str(a.candidate)+' qualification', expected, flush=True)


if __name__ == '__main__':
    main()
