# SPDX-License-Identifier: MPL-2.0
"""Asset-free synthetic tests; optional private original-data equivalence audit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SDK = Path('/root/lilsmoke-os/build/buildroot/output')
CROSS = SDK / 'host/bin/mipsel-buildroot-linux-musl-'
FLAGS = ['-mips32r2', '-mabi=32', '-mfp32', '-mhard-float', '-mdspr2']
DATA = [('gBankSetsData', 'bank_sets'), ('gMusicData', 'sequences.bin'),
        ('gSoundDataRaw', 'sound_data.tbl'), ('gSoundDataADSR', 'sound_data.ctl')]


def symbols(path):
    output = subprocess.run([str(CROSS) + 'readelf', '-sW', str(path)], check=True,
        text=True, capture_output=True).stdout
    result = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[-1] in dict(DATA):
            result[fields[-1]] = dict(offset=int(fields[1], 16), size=int(fields[2], 0),
                kind=fields[3], binding=fields[4], visibility=fields[5])
    return result


def section(path, out):
    subprocess.run([str(CROSS) + 'objcopy', '-O', 'binary', '-j', '.data', str(path), str(out)],
        check=True, capture_output=True)
    return out.read_bytes()


def assemble(work, output, compiler, env=None):
    subprocess.run([str(compiler), '-nostdinc', *FLAGS, '-c', '-x', 'assembler-with-cpp',
        str(HERE / 'sound_data.S'), '-o', str(output)], cwd=work, env=env,
        check=True, capture_output=True)


def native_env(runtime):
    return dict(os.environ, QEMU_CPU='74Kf', QEMU_LD_PREFIX=str(SDK / 'staging'),
        LD_LIBRARY_PATH=str(runtime / 'lib') + ':' + str(SDK / 'staging/usr/lib') + ':' + str(SDK / 'staging/lib'))


class SoundEmbedding(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sound embedding ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        sound = self.root / 'build/original-mips/sound'
        sound.mkdir(parents=True)
        self.bytes = {}
        for index, (name, filename) in enumerate(DATA):
            raw = bytes((byte * 73 + index * 19) & 255 for byte in range(16 * (index + 2)))
            (sound / filename).write_bytes(raw)
            self.bytes[name] = raw

    def check(self, compiler, env=None):
        original = self.root / 'reference.c'
        original.write_text('\n'.join('unsigned char ' + name + '[] = {' +
            ','.join(map(str, self.bytes[name])) + '};' for name, _ in reversed(DATA)))
        c_object = self.root / 'reference.o'
        subprocess.run([str(compiler), *FLAGS, '-O2', '-fcommon', '-c', str(original), '-o', str(c_object)],
            check=True, capture_output=True, env=env)
        embedded = self.root / 'embedded.o'
        assemble(self.root, embedded, compiler, env)
        actual = section(embedded, self.root / 'embedded.data')
        self.assertEqual(actual, section(c_object, self.root / 'reference.data'))
        self.assertEqual(symbols(embedded), symbols(c_object))
        for name, _ in DATA:
            row = symbols(embedded)[name]
            self.assertEqual(actual[row['offset']:row['offset'] + row['size']], self.bytes[name])
            self.assertEqual((row['kind'], row['binding']), ('OBJECT', 'GLOBAL'))

    def test_cross_mips_matches_original_c_data_and_symbols(self):
        self.check(str(CROSS) + 'gcc')

    def test_native_mips_matches_original_c_data_and_symbols(self):
        runtime = Path('/root/sm64-prep3-payload-20261016-ram/Super Mario 64/runtime')
        self.check(runtime / 'compiler/wrappers/cc', native_env(runtime))


def private_audit(work, runtime, out):
    out.mkdir(parents=True, exist_ok=False)
    original = work / 'build/original-mips/sound/sound_data.o'
    result = out / 'PRIVATE-audio-assets.o'
    assemble(work, result, runtime / 'compiler/wrappers/cc', native_env(runtime))
    before = section(original, out / 'PRIVATE-reference.data')
    after = section(result, out / 'PRIVATE-embedded.data')
    if before != after or symbols(original) != symbols(result):
        raise ValueError('Audio binary layout differs from the original compiled game')
    for name, filename in DATA:
        row = symbols(result)[name]
        raw = (work / 'build/original-mips/sound' / filename).read_bytes()
        if after[row['offset']:row['offset'] + row['size']] != raw:
            raise ValueError('Embedded audio bytes differ: ' + name)
    report = dict(passed=True, data_bytes=len(after),
        assembly_source_sha256=hashlib.sha256((HERE / 'sound_data.S').read_bytes()).hexdigest(),
        original_data_sha256=hashlib.sha256(before).hexdigest(),
        embedded_data_sha256=hashlib.sha256(after).hexdigest(),
        all_four_symbol_offsets_sizes_and_bindings_equal=True,
        native_mips_assembler=True, raw_rom_assets_private=True,
        game_audio_bytes_unchanged=True, physical_ram_acceptance=False)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    if '--private-work' in sys.argv:
        parser = argparse.ArgumentParser()
        parser.add_argument('--private-work', type=Path, required=True)
        parser.add_argument('--runtime', type=Path, required=True)
        parser.add_argument('--out', type=Path, required=True)
        args = parser.parse_args()
        private_audit(args.private_work, args.runtime, args.out)
    else:
        unittest.main()
