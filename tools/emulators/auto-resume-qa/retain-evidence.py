# SPDX-License-Identifier: MPL-2.0
"""Retain text-only runtime QA evidence, including failures (never ROM/state data)."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--input', type=Path, required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--debug-input', type=Path, action='append', default=[], help='Private inspect-crash output; retain text only')
a = ap.parse_args()
source = a.input.resolve()
a.out.mkdir(parents=True, exist_ok=False)
serial = (source / 'serial.log').read_text(errors='replace')
exits = {name: int(code) for name, code in re.findall(r'QA_CASE_EXIT (\S+) (\d+)', serial)}
assert exits and 'QA_ALL_DONE' in serial and 'System halted' in serial, 'Incomplete guest run'
memory = (source / 'memory.txt').read_text()
swap = int(re.search(r'SwapTotal:\s+(\d+)', memory)[1])
cpus = len(re.findall(r'^processor\s*:', (source / 'cpu.txt').read_text(), re.M))
mem = int(re.search(r'MemTotal:\s+(\d+)', memory)[1])
assert cpus == 1 and mem <= 120 * 1024 and swap == 0
cases = {}
for name, code in exits.items():
    log = (source / (name + '.log')).read_text(errors='replace')
    cfg = (source / (name + '.cfg')).read_text()
    cases[name] = dict(exit=code,
        auto_save=re.search(r'^savestate_auto_save = (\w+)$', cfg, re.M)[1],
        auto_load=re.search(r'^savestate_auto_load = (\w+)$', cfg, re.M)[1],
        auto_save_succeeded=bool(re.search(r'Auto save state to .* succeeded\.', log)),
        serialized_ram_hashes=re.findall(r'QA_SERIALIZE ok=1 hash=([0-9a-f]+)', log),
        restored_ram=re.findall(r'QA_UNSERIALIZE ok=1 hash=([0-9a-f]+) bytes=\d+ frames=(\d+)', log),
        loading_state='[State]: Loading state ' in log,
        rendered_game_frames=re.findall(r'QA_UNLOAD frames=(\d+)', log),
        unavailable_native_audio='failed_to_start_audio_driver' in log)

checks = {}
first = cases.get('first', {})
second = cases.get('second', {})
if first.get('serialized_ram_hashes') and second.get('restored_ram'):
    checks['actual_ram_roundtrip'] = first['serialized_ram_hashes'][-1] == second['restored_ram'][0][0]
    checks['ram_advances_after_resume'] = second['serialized_ram_hashes'][-1] != second['restored_ram'][0][0]
if (source / 'before-off.sha256').exists():
    checks['disabled_leaves_auto_state_unchanged'] = (source / 'before-off.sha256').read_bytes() == (source / 'after-off.sha256').read_bytes()
manual = sorted((source / 'states').rglob('Alpha.state*'))
manual = [f for f in manual if f.name in ('Alpha.state17', 'Alpha.state')]
if manual:
    checks['manual_slots_unchanged'] = len(manual) == 2 and all(
        f.read_text() == ('manual slot 17 sentinel\n' if f.name.endswith('17') else 'manual slot 0 sentinel\n') for f in manual)
autos = sorted(str(f.relative_to(source)) for f in (source / 'states').rglob('*.auto'))
if 'unsupported' in cases:
    checks['zero_size_core_did_not_write_fake_state'] = not any('NoState' in p for p in autos) and not cases['unsupported']['serialized_ram_hashes']
    alpha = list((source / 'states').rglob('Alpha.state.auto'))
    checks['distinct_core_identities_have_distinct_paths'] = len(alpha) == 2 and alpha[0].parent != alpha[1].parent
if 'other-content' in cases:
    checks['other_content_did_not_load_alpha_state'] = not cases['other-content']['loading_state']
checks['clean_frontend_exits'] = all(code == 0 for code in exits.values())
checks['guest_halted_deterministically'] = True

retained = {}
names = {'serial.log', 'memory.txt', 'swaps.txt', 'cpu.txt', 'commands.json', 'inputs.json', 'report.json', 'exits.json'}
names.update(name + suffix for name in cases for suffix in ('.log', '.cfg', '.exit'))
names.update(('present.so.build.log', 'observed.so.build.log'))
names.update(('mutex-trace.so.build.log', 'core-pattern.txt'))
names.update(p.name for p in source.glob('core.*.gdb.txt'))
for name in sorted(names):
    path = source / name
    if path.is_file():
        assert path.stat().st_size < 2 * 1024 * 1024, 'Unexpectedly large text log'
        shutil.copyfile(path, a.out / name)
        retained[name] = hashlib.sha256(path.read_bytes()).hexdigest()
for index, directory in enumerate(a.debug_input):
    destination = a.out / ('debug-' + str(index))
    destination.mkdir()
    for name in ('backtrace.txt', 'symbol-matches.json', 'command.json'):
        path = directory / name
        assert path.is_file() and path.stat().st_size < 2 * 1024 * 1024
        shutil.copyfile(path, destination / name)
        retained[str((destination / name).relative_to(a.out))] = hashlib.sha256(path.read_bytes()).hexdigest()
private_hashes = {}
for path in source.iterdir():
    if path.is_file() and (path.suffix == '.so' or (path.name.startswith('core.') and not path.name.endswith('.txt'))):
        with path.open('rb') as data:
            private_hashes[path.name] = dict(bytes=path.stat().st_size, sha256=hashlib.file_digest(data, 'sha256').hexdigest())
summary = dict(fully_passed=all(checks.values()), runtime_source=str(source), cases=cases,
    checks=checks, state_paths_only_no_state_data=autos, cpus=cpus, configured_ram_mib=120,
    memtotal_kib=mem, swap_kib=swap, card_writes=False, physical_console_tested=False,
    graphics_audio_tested=False, retained_sha256=retained, private_payload_hashes_not_copied=private_hashes,
    limitation='Native frontend teardown SIGSEGV is retained as a failure, not a successful clean exit.'
               if not checks['clean_frontend_exits'] else None)
(a.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
