"""Stage one qualified player and a proposed manifest on the PC, never a card."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil

P = Path(__file__).resolve().parents[2]
BASELINE = '66573ccaa3a8a36fb73452ad12e6bfa2944be8ae7a4ba4aed16fd7a36e9eac46'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qualification', type=Path, required=True)
    parser.add_argument('--baseline-runtime', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    a = parser.parse_args()
    q = json.loads((a.qualification/'report.json').read_text())
    required = {'optimized-profile', 'build-overlay-host', 'overlay-host', 'build-overlay-mips', 'overlay-mips',
                'video-recovery', 'native-protocol', 'pcm-regression', 'build-host-asan', 'host-controls-formats',
                'build-mips', 'rebuild-mips', 'build-sync-host', 'sync-host-owner.mp4', 'sync-host-owner.webm',
                'build-sync-mips', 'sync-mips-owner.mp4', 'sync-mips-owner.webm', 'vm-video', 'vm-audio'}
    assert q['passed'] and set(q['gates'])==required and all(g['passed'] for g in q['gates'].values())
    for name in ('owner.mp4', 'owner.webm'):
        result = q['comparisons']['mips'][name]
        assert result['cpu_speedup']>1.1 and result['all_frames_and_pts_retained'] and result['pcm_bit_identical']
    for path, digest in q['source_sha256'].items():
        assert sha(P/path) == digest, path
    binary = a.qualification/'mips/lilsmoke-media'
    assert sha(binary) == q['binary_sha256'] == sha(a.qualification/'rebuild/lilsmoke-media')
    assert sha(a.baseline_runtime) == BASELINE
    rows = list(csv.reader(io.StringIO(a.baseline_runtime.read_text()), delimiter='\t'))
    before = [row.copy() for row in rows]
    assert all(len(row)==4 for row in rows)
    selected = [row for row in rows if row[2]=='usr/bin/lilsmoke-media']
    assert len(selected)==1 and selected[0]==['F', '0755', 'usr/bin/lilsmoke-media', 'candidate47/000001.bin']
    selected[0][3] = 'media-video-perf-20261027/000001.bin'
    assert sum(x!=y for x,y in zip(rows,before))==1
    a.out.mkdir(parents=True, exist_ok=False)
    shutil.copy2(binary, a.out/'000001.bin')
    assert sha(a.out/'000001.bin')==q['binary_sha256']
    with (a.out/'runtime-proposed.tsv').open('w', newline='') as f:
        csv.writer(f, delimiter='\t', lineterminator='\n').writerows(rows)
    reports = {'qualification-report.json':a.qualification/'report.json',
               'comparison.json':a.qualification/'comparison.json',
               'paired-samples.json':a.qualification/'paired-samples.json',
               'baseline-profile-report.json':a.qualification.parent/'baseline/report.json',
               'optimized-profile-report.json':a.qualification.parent/'optimized/report.json'}
    for label in ('video-recovery', 'native-protocol', 'pcm-regression', 'host-controls-formats', 'vm-video', 'vm-audio'):
        report = a.qualification/label/'report.json'
        assert json.loads(report.read_text())['passed']
        reports[label+'-report.json'] = report
    for name, source in reports.items():
        shutil.copy2(source, a.out/name)
    logs = list(a.qualification.glob('overlay-*.log'))+list(a.qualification.glob('sync-*.log'))
    for source in logs:
        shutil.copy2(source, a.out/source.name)
    payload = dict(expected_manifest_sha256=BASELINE, proposed_manifest_sha256=sha(a.out/'runtime-proposed.tsv'),
        files=[dict(target='usr/bin/lilsmoke-media', object='000001.bin', mode='0755',
                    bytes=binary.stat().st_size, sha256=q['binary_sha256'],
                    previous_sha256='29e7e734db40bf236fc0a3f21b6521231c69724bd078eda082a4bbbc32cde8ee')],
        qualification_sha256=sha(a.out/'qualification-report.json'),
        worker_unchanged_sha256='36cc7e5922ec2cd9e2586225090a4bddeabcdf3ebc8de375b626a5b31fdc3e75',
        kernel_change=False, sm64_change=False, daedalus_bundled=False, sd_writes=False,
        hardware_m8_timing_measured=False, private_inputs_bundled=False)
    (a.out/'payload.json').write_text(json.dumps(payload, indent=2)+'\n')
    print(json.dumps(payload, indent=2))


if __name__ == '__main__':
    main()
