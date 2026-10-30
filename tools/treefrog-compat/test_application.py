#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Host-only app boundary tests. No SD paths, mounts, game execution or full VM."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROBE = Path(os.environ.get('TREEFROG_PROBE', str(HERE / '_build/host-20261029-compat02/application-probe')))


def elf(abi='glibc', frontend=False):
    data = bytearray(512)
    interpreter = b'/lib/ld.so.1\0' if abi == 'glibc' else b'/lib/ld-musl-mipsel.so.1\0'
    strings = b'\0libc.so.6\0' if abi == 'glibc' else b'\0libc.so\0'
    data[:52] = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9),
                            2 if frontend else 3, 8, 1, 0, 52, 0, 0x70001000, 52, 32, 2, 0, 0, 0)
    data[52:84] = struct.pack('<IIIIIIII', 1, 0, 0x1000, 0, 512, 512, 4, 4)
    data[84:116] = struct.pack('<IIIIIIII', 3 if frontend else 2, 128, 0, 0,
                              len(interpreter) if frontend else 32, 32, 4, 4)
    if frontend:
        data[128:128+len(interpreter)] = interpreter
    else:
        data[128:160] = struct.pack('<IIIIIIII', 5, 0x1100, 10, len(strings), 1, 1, 0, 0)
        data[256:256+len(strings)] = strings
    return data


class ApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = Path('/storage/independent-apps')
        base.mkdir(parents=True, exist_ok=True)
        cls.app = Path(tempfile.mkdtemp(prefix='tf-test-renamed app ', dir=base))
        cls.port = cls.app / 'runtime/port'
        cls.port.mkdir(parents=True)
        cls.config = dict(schema_version=1, id='classicube', name='ClassiCube',
                          launch=dict(type='libretro', binary='engine.so', content='content.cc'),
                          required_file_sets=[])
        cls.adapter = dict(schema=1, kind='treefrog-converted', port_id='classicube',
                           profile='libretro', binary='engine.so', content='content.cc',
                           port_root='runtime/port', requires_runtime='treefrog-compat-0.2',
                           binary_recompiled=False, independent_libretro=True,
                           frontend='runtime/frontend/lilretro', runtime_profile='glibc')
        (cls.app / 'classicube.sh').write_text('#!/bin/sh\nexit 0\n')
        (cls.app / 'meta.json').write_text(json.dumps(dict(
            id='org.lilsmoke.treefrog.classicube', name='Renamed Application', version='0.0.1')))
        for name in ('run.sh', 'content.cc'):
            (cls.port / name).write_bytes(b'original fixture\n')
        (cls.app / 'runtime/frontend').mkdir()
        (cls.app / 'runtime/launch.json').write_text('{"schema":1,"save_directory":"userdata"}')

    @classmethod
    def tearDownClass(cls):
        assert cls.app.resolve().parent == Path('/storage/independent-apps')
        assert cls.app.name.startswith('tf-test-renamed app ')
        shutil.rmtree(cls.app)

    def setUp(self):
        (self.port / 'config.json').write_text(json.dumps(self.config))
        (self.app / 'adapter.json').write_text(json.dumps(self.adapter))
        (self.port / 'engine.so').write_bytes(elf())
        (self.app / 'runtime/frontend/lilretro').write_bytes(elf(frontend=True))
        (self.app / 'runtime/launch.json').write_text('{"schema":1,"save_directory":"userdata"}')

    def probe(self, path=None):
        return subprocess.run([str(PROBE), str(path or self.app)], capture_output=True, text=True)

    def reject(self, adapter):
        (self.app / 'adapter.json').write_text(json.dumps(adapter))
        self.assertNotEqual(self.probe().returncode, 0)

    def test_renamed_folder_and_byte_preservation(self):
        before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.port.iterdir()}
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'classicube libretro\n')
        self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.port.iterdir()})

    def test_schema_and_contract(self):
        for key, value in [('schema', True), ('schema', 2), ('port_id', '../x'),
                           ('port_root', '../port'), ('kind', 'shell'),
                           ('binary_recompiled', True), ('requires_runtime', 'anything')]:
            with self.subTest(key=key, value=value):
                bad = dict(self.adapter, **{key: value})
                self.reject(bad)

    def test_arbitrary_launcher_name(self):
        launcher = self.app / 'classicube.sh'
        renamed = self.app / 'Anything You Like.SH'
        launcher.rename(renamed)
        try:
            result = self.probe()
            self.assertEqual(result.returncode, 0, result.stderr)
            launcher.write_text('#!/bin/sh\nexit 0\n')
            self.assertNotEqual(self.probe().returncode, 0)
        finally:
            launcher.unlink(missing_ok=True)
            renamed.rename(launcher)

    def test_config_disagreement(self):
        for key, value in [('profile', 'native'), ('binary', 'other.so'), ('content', 'other')]:
            self.reject(dict(self.adapter, **{key: value}))

    def test_path_and_metadata(self):
        for path in ['/tmp/app', '/storage/independent-apps',
                     str(self.app / '..'), str(self.app) + '/runtime']:
            self.assertNotEqual(self.probe(path).returncode, 0)
        saved = (self.app / 'meta.json').read_bytes()
        try:
            (self.app / 'meta.json').write_text('{"id":"wrong","name":"X","version":"0.0.1"}')
            self.assertNotEqual(self.probe().returncode, 0)
        finally:
            (self.app / 'meta.json').write_bytes(saved)

    def test_symlink_and_hardlink_data(self):
        link = self.port / 'escape'
        try:
            link.symlink_to('/tmp')
            self.assertNotEqual(self.probe().returncode, 0)
            link.unlink()
            os.link(self.port / 'content.cc', link)
            self.assertNotEqual(self.probe().returncode, 0)
        finally:
            link.unlink(missing_ok=True)

    def test_symlink_metadata(self):
        target = self.app / 'adapter.json'
        saved = target.read_bytes()
        try:
            target.unlink()
            target.symlink_to(self.port / 'config.json')
            self.assertNotEqual(self.probe().returncode, 0)
        finally:
            target.unlink()
            target.write_bytes(saved)

    def test_data_alternatives(self):
        config = copy.deepcopy(self.config)
        config['required_file_sets'] = [['missing.pak'], ['content.cc']]
        (self.port / 'config.json').write_text(json.dumps(config))
        self.assertEqual(self.probe().returncode, 0)
        config['required_file_sets'] = [['missing.pak']]
        (self.port / 'config.json').write_text(json.dumps(config))
        self.assertNotEqual(self.probe().returncode, 0)

    def test_native_without_content(self):
        config = copy.deepcopy(self.config)
        config['launch'] = dict(type='native', binary='engine.so')
        (self.port / 'config.json').write_text(json.dumps(config))
        adapter = dict(self.adapter, profile='native', content='')
        (self.app / 'adapter.json').write_text(json.dumps(adapter))
        (self.port / 'engine.so').write_bytes(elf(frontend=True))
        self.assertEqual(self.probe().returncode, 0)

    def test_independent_frontend_contract(self):
        for key, value in [('independent_libretro', False), ('frontend', '../frontend'),
                           ('frontend', 'runtime/frontend/elsewhere'), ('runtime_profile', 'unknown')]:
            self.reject(dict(self.adapter, **{key: value}))

    def test_abi_mismatch_and_unknown(self):
        (self.app / 'runtime/frontend/lilretro').write_bytes(elf('musl', frontend=True))
        self.assertNotEqual(self.probe().returncode, 0)
        (self.port / 'engine.so').write_bytes(elf('musl'))
        (self.app / 'adapter.json').write_text(json.dumps(dict(self.adapter, runtime_profile='musl')))
        self.assertEqual(self.probe().returncode, 0)
        (self.port / 'engine.so').write_bytes(b'not an ELF')
        self.assertNotEqual(self.probe().returncode, 0)

    def test_mips_abi_flags(self):
        for flag in (0x20, 0x200, 0x400):
            data = elf()
            struct.pack_into('<I', data, 36, 0x70001000 | flag)
            (self.port / 'engine.so').write_bytes(data)
            self.assertNotEqual(self.probe().returncode, 0)

    def test_picoarch_frontend_return_guard(self):
        with tempfile.TemporaryDirectory(prefix='tf-return-guard-') as temp:
            wrapper = Path(temp) / 'picoarch'
            shutil.copyfile(ROOT / 'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch', wrapper)
            wrapper.chmod(0o700)
            env = dict(os.environ, LILSMOKE_TREEFROG_CORE='/mnt/sdcard/roms/ports/classicube/classicube_libretro.so')
            result = subprocess.run([str(wrapper), '/mnt/sdcard/cubegm/cores/frogui_libretro.so'],
                                    capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('legacy frontend handoff suppressed', result.stdout)

    def test_save_containment(self):
        for value in ['/tmp/escape', '../escape', 'safe/../../escape', '.']:
            (self.app / 'runtime/launch.json').write_text(json.dumps(dict(schema=1, save_directory=value)))
            self.assertNotEqual(self.probe().returncode, 0)


if __name__ == '__main__':
    unittest.main()
