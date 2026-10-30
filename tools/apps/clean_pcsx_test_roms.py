"""Remove only disposable ROM/cache copies from our completed PCSX VM fixtures."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil

P = Path(__file__).resolve().parents[2]
ROOT = Path('/root')
names = ['pcsx-app-qa-20261011'] + [f'pcsx-app-qa-20261011-retry{i}' for i in range(1, 5)]
parser = argparse.ArgumentParser()
parser.add_argument('--candidate24', action='store_true')
parser.add_argument('--candidate25', action='store_true')
args = parser.parse_args()
if args.candidate24:
    names = ['pcsx-app-qa-20261011-candidate24', 'pcsx-app-qa-20261011-c24ui']
if args.candidate25:
    names = ['pcsx-app-qa-20261011-c25']
user_rom = Path('/path/to/local-input/Crash Bandicoot (USA).zip')


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


expected = digest(user_rom)
evidence = P / ('test-results/sm64-local-usb-gamepads-2026-10-11' if args.candidate25 else 'test-results/pcsx4all-launch-sm64-2026-10-11' if args.candidate24 else 'test-results/pcsx4all-app-2026-10-11')
report_source = ROOT/names[-1]/'report.json' if args.candidate24 or args.candidate25 else evidence/'pcsx-vm/report.json'
assert json.loads(report_source.read_text())['passed']
if args.candidate24 or args.candidate25:
    destination = evidence/'pcsx-vm'
    destination.mkdir(exist_ok=True)
    for name in ['report.json', 'memory.txt', 'pcsx4all.log', 'serial.log', 'ui.log'] + [f.name for f in (ROOT/names[-1]).glob('*.png')]:
        source = ROOT/names[-1]/name
        if source.is_file():
            shutil.copyfile(source, destination/name)
removed = []
for name in names:
    fixture = ROOT / name
    assert fixture.parent == ROOT and not fixture.is_symlink()
    if not fixture.is_dir():
        continue
    # All named fixtures were created by this test; their source markers remain.
    assert (fixture / 'serial.log').is_file() and (fixture / 'boot.cpio.gz').is_file()
    copy = fixture / 'storage/roms/psx/Crash-test.zip'
    if copy.exists():
        assert copy.resolve() == copy and not copy.is_symlink()
        assert digest(copy) == expected, 'Refusing to remove an unexpected ROM'
        removed.append({'path': str(copy), 'bytes': copy.stat().st_size})
        copy.unlink()
    cache = fixture / 'storage/.cache/lilsmoke/archives'
    if cache.exists():
        assert cache.resolve() == cache and not cache.is_symlink()
        entries = list(cache.rglob('*'))
        assert all(not entry.is_symlink() for entry in entries)
        size = sum(entry.stat().st_size for entry in entries if entry.is_file())
        removed.append({'path': str(cache), 'bytes': size})
        shutil.rmtree(cache)
assert digest(user_rom) == expected
report = {'user_rom_unchanged': True, 'test_evidence_preserved': True,
          'removed': removed, 'logical_bytes_removed': sum(item['bytes'] for item in removed)}
(evidence / 'disposable-rom-cleanup.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
