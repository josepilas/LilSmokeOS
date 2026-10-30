#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only synthetic converter audit. No fixture programs are executed.

Run with python3 -B tests/security_qa.py [--wasm] [--audit]. The only optional
write is ../_build/audit.json. Failures are intentional regression assertions,
not expected-failure annotations: a nonzero exit means an invariant is broken.
The prebuilt native library and WASM are tested without rebuilding either.
"""
import argparse
import base64
import ctypes
import datetime
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import unittest
import warnings
import zipfile

sys.dont_write_bytecode = True
import test_core as fixtures

ROOT = Path(__file__).resolve().parents[1]


def zip_bytes(files, compression=zipfile.ZIP_DEFLATED, modes=None, methods=None):
    entries = files.items() if isinstance(files, dict) else files
    out = io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(out, 'w', compression) as archive:
            for name, data in entries:
                info = zipfile.ZipInfo(name)
                info.compress_type = (methods or {}).get(name, compression)
                info.create_system = 3
                mode = (modes or {}).get(name, stat.S_IFREG | 0o644)
                if name.endswith('/'):
                    mode = stat.S_IFDIR | 0o755
                    info.external_attr = (mode << 16) | 0x10
                else:
                    info.external_attr = mode << 16
                archive.writestr(info, data)
    return out.getvalue()


def configured(**changes):
    files = fixtures.port()
    config = json.loads(files['test/config.json'])
    config.update(changes)
    files['test/config.json'] = json.dumps(config).encode()
    return files


def native(**launch_changes):
    files = fixtures.port()
    config = json.loads(files['test/config.json'])
    config['launch'] = dict(type='native', binary='core.so', **launch_changes)
    files['test/config.json'] = json.dumps(config).encode()
    files['test/core.so'] = fixtures.elf(api=False)
    files['test/run.sh'] = b'#!/bin/sh\nexec ./core.so\n'
    return files


def corrupt_payload(data, name):
    out = bytearray(data)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        info = archive.getinfo(name)
        start = info.header_offset
        filename, extra = struct.unpack_from('<HH', out, start + 26)
        start += 30 + filename + extra
        out[start + info.compress_size // 2] ^= 0x40
    return bytes(out)


def symbol_variant(binding=1, kind=2, visibility=0, defined=True):
    out = bytearray(fixtures.elf())
    section = struct.unpack_from('<I', out, 32)[0] + 3 * 40
    offset, size = struct.unpack_from('<II', out, section + 16)
    for pos in range(offset + 16, offset + size, 16):
        out[pos + 12] = (binding << 4) | kind
        out[pos + 13] = visibility
        struct.pack_into('<H', out, pos + 14, 1 if defined else 0)
    return bytes(out)


def program_dynamic(libs=('libunseen.so', 'libc.so.6'), interpreter=None):
    """Keep innocuous section metadata, add a different loader PT_DYNAMIC view.

    PT_LOAD maps the new tables and program headers consistently. These bytes
    are metadata probes, not executable or gameplay-qualified MIPS programs.
    """
    out = bytearray(fixtures.elf(api=interpreter is None))
    strings = bytearray(b'\0')
    offsets = []
    for name in libs:
        offsets.append(len(strings))
        strings += name.encode() + b'\0'
    strings_at = len(out)
    out += strings
    dynamic_at = len(out)
    dynamic = b''.join(struct.pack('<II', 1, off) for off in offsets)
    dynamic += struct.pack('<IIIIII', 5, 0x1000 + strings_at,
                           10, len(strings), 0, 0)
    out += dynamic
    interp_at = len(out)
    if interpreter:
        out += interpreter.encode() + b'\0'
    phoff = len(out)
    count = 3 if interpreter else 2
    total = len(out) + count * 32
    out += struct.pack('<IIIIIIII', 1, 0, 0x1000, 0, total, total, 5, 4096)
    out += struct.pack('<IIIIIIII', 2, dynamic_at, 0x1000 + dynamic_at,
                       0, len(dynamic), len(dynamic), 4, 4)
    if interpreter:
        size = len(interpreter) + 1
        out += struct.pack('<IIIIIIII', 3, interp_at, 0x1000 + interp_at,
                           0, size, size, 4, 1)
    struct.pack_into('<I', out, 28, phoff)
    struct.pack_into('<H', out, 44, count)
    return bytes(out)


class NativeEngine:
    def __init__(self):
        self.c = ctypes.CDLL(str(ROOT / '_build/libtfconvert.so'))
        for name in ('tf_open', 'tf_runtime'):
            getattr(self.c, name).argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.c.tf_report.restype = self.c.tf_error.restype = ctypes.c_char_p
        self.c.tf_convert.argtypes = [ctypes.c_uint, ctypes.c_char_p, ctypes.c_char_p]
        self.c.tf_output_data.restype = ctypes.c_void_p
        self.c.tf_output_size.restype = ctypes.c_size_t
        self.buffers = []

    def attach(self, method, data):
        buffer = ctypes.create_string_buffer(data)
        self.buffers.append(buffer)
        return getattr(self.c, 'tf_' + method)(buffer, len(data))

    def open(self, data):
        self.close()
        return self.attach('open', data)

    def runtime(self, data):
        return self.attach('runtime', data)

    def report(self):
        return json.loads(self.c.tf_report())

    def convert(self):
        return self.c.tf_convert(0, b'Audit Game', b'1.2.3')

    def error(self):
        return self.c.tf_error().decode('utf-8', errors='replace')

    def output(self):
        size = self.c.tf_output_size()
        return ctypes.string_at(self.c.tf_output_data(), size) if size else b''

    def close(self):
        self.c.tf_close()
        self.buffers.clear()

    def shutdown(self):
        self.close()


NODE_BRIDGE = r'''
const fs = require('fs');
const readline = require('readline');
const path = require('path');
(async () => {
 const root = process.argv[1];
 const create = require(path.join(root, 'site/dist/converter.js'));
 const m = await create({wasmBinary:fs.readFileSync(path.join(root,'site/dist/converter.wasm')),
                         print:()=>{}, printErr:()=>{}});
 let held=[];
 function close(){m._tf_close();for(const ptr of held)m._free(ptr);held=[];}
 function bytes(b64){const b=Buffer.from(b64,'base64');const p=m._malloc(b.length);
   if(!p)throw Error('allocation failed');held.push(p);m.HEAPU8.set(b,p);return [p,b.length];}
 for await(const line of readline.createInterface({input:process.stdin,crlfDelay:Infinity})){
  try {
   const q=JSON.parse(line);let result;
   if(q.method==='open'){close();result=m._tf_open(...bytes(q.data));}
   else if(q.method==='runtime')result=m._tf_runtime(...bytes(q.data));
   else if(q.method==='report')result=JSON.parse(m.UTF8ToString(m._tf_report()));
   else if(q.method==='error')result=m.UTF8ToString(m._tf_error());
   else if(q.method==='output'){const p=m._tf_output_data(),n=m._tf_output_size();
      result=n?Buffer.from(m.HEAPU8.subarray(p,p+n)).toString('base64'):'';}
   else if(q.method==='convert'){
    const a=bytes(Buffer.from('Audit Game\0').toString('base64'))[0];
    const b=bytes(Buffer.from('1.2.3\0').toString('base64'))[0];
    result=m._tf_convert(0,a,b);
   }else if(q.method==='close'){close();result=true;}
   else throw Error('unknown method');
   process.stdout.write(JSON.stringify({result})+'\n');
  }catch(error){process.stdout.write(JSON.stringify({error:String(error.stack||error)})+'\n');}
 }
 close();
})().catch(error=>{process.stderr.write(String(error));process.exitCode=1;});
'''


class WasmEngine:
    def __init__(self):
        node = shutil.which('node')
        if not node:
            raise RuntimeError('WASM audit requested but node is unavailable')
        self.process = subprocess.Popen([node, '-e', NODE_BRIDGE, str(ROOT)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True)

    def request(self, method, data=None):
        request = dict(method=method)
        if data is not None:
            request['data'] = base64.b64encode(data).decode()
        self.process.stdin.write(json.dumps(request) + '\n')
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('WASM bridge stopped: ' + self.process.stderr.read())
        answer = json.loads(line)
        if 'error' in answer:
            raise RuntimeError(answer['error'])
        return answer['result']

    def open(self, data):
        return self.request('open', data)

    def runtime(self, data):
        return self.request('runtime', data)

    def report(self):
        return self.request('report')

    def convert(self):
        return self.request('convert')

    def error(self):
        return self.request('error')

    def output(self):
        return base64.b64decode(self.request('output'))

    def close(self):
        self.request('close')

    def shutdown(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()
        self.process.stderr.close()


class SecurityTests(unittest.TestCase):
    backend = NativeEngine

    @classmethod
    def setUpClass(cls):
        cls.engine = cls.backend()

    @classmethod
    def tearDownClass(cls):
        cls.engine.shutdown()

    def tearDown(self):
        self.engine.close()

    def open(self, files, **kwargs):
        return self.engine.open(zip_bytes(files, **kwargs))

    def attach_runtime(self, files=None):
        files = files or {'glibc/lilretro': fixtures.elf(api=False),
                          'licenses/runtime.txt': b'Synthetic test license'}
        self.assertEqual(self.engine.runtime(zip_bytes(files)), 1, self.engine.error())

    def ready(self, files):
        self.assertEqual(self.open(files), 1, self.engine.error())
        self.attach_runtime()
        return self.engine.report()['ports'][0]

    def assert_blocked(self, files):
        if self.open(files) == 0:
            return
        self.attach_runtime()
        port = self.engine.report()['ports'][0]
        self.assertFalse(port['ready'], 'Unsafe/unsupported fixture marked ready: ' +
                         json.dumps(port, ensure_ascii=True)[:1000])
        self.assertEqual(self.engine.convert(), 0)

    def package(self):
        self.assertEqual(self.engine.convert(), 1, self.engine.error())
        return zipfile.ZipFile(io.BytesIO(self.engine.output()))

    def test_crc_corruption_fails_atomically(self):
        for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            with self.subTest(compression=compression):
                data = corrupt_payload(zip_bytes(fixtures.port(), methods={'test/Data.rsdk': compression}),
                                       'test/Data.rsdk')
                self.assertEqual(self.engine.open(data), 1)
                self.attach_runtime()
                self.assertEqual(self.engine.convert(), 0)
                self.assertIn('CRC', self.engine.error())
                self.assertEqual(self.engine.output(), b'')

    def test_valid_stored_and_mixed_compression_archives_convert(self):
        cases = [dict(compression=zipfile.ZIP_STORED),
                 dict(methods={'test/Data.rsdk': zipfile.ZIP_STORED})]
        for options in cases:
            with self.subTest(options=options):
                self.assertEqual(self.open(fixtures.port(), **options), 1)
                self.attach_runtime()
                with self.package() as archive:
                    self.assertIsNone(archive.testzip())
                    self.assertEqual(archive.read('runtime/port/Data.rsdk'),
                                     fixtures.port()['test/Data.rsdk'])

    def test_bad_runtime_payload_crc_fails_atomically(self):
        self.assertEqual(self.open(fixtures.port()), 1)
        runtime = zip_bytes({'glibc/lilretro': fixtures.elf(api=False),
                             'licenses/runtime.txt': b'Test-only license bytes'})
        self.assertEqual(self.engine.runtime(corrupt_payload(runtime, 'licenses/runtime.txt')), 1)
        self.assertEqual(self.engine.convert(), 0)
        self.assertIn('CRC', self.engine.error())
        self.assertEqual(self.engine.output(), b'')

    def test_duplicate_basename_in_distinct_directories_is_preserved(self):
        files = fixtures.port()
        files.update({'test/a/same.dat': b'one', 'test/b/same.dat': b'two'})
        self.assertTrue(self.ready(files)['ready'])
        with self.package() as archive:
            self.assertEqual(archive.read('runtime/port/a/same.dat'), b'one')
            self.assertEqual(archive.read('runtime/port/b/same.dat'), b'two')

    def test_duplicate_case_and_file_prefix_collisions_rejected(self):
        pairs = [ [('test/core.so', b'x')], [('test/CORE.SO', b'x')],
                  [('test/Data.rsdk/child', b'x')],
                  [('test/node/child', b'x'), ('test/node', b'x')],
                  [('test/node', b'x'), ('test/NODE/child', b'x')],
                  [('test/node/', b''), ('test/node', b'x')] ]
        for extra in pairs:
            with self.subTest(extra=[name for name, _ in extra]):
                self.assertEqual(self.open([*fixtures.port().items(), *extra]), 0)

    def test_zip_symlink_and_special_modes_rejected(self):
        for mode in (stat.S_IFLNK, stat.S_IFIFO, stat.S_IFCHR, stat.S_IFSOCK):
            with self.subTest(mode=oct(mode)):
                files = fixtures.port()
                files['test/link'] = b'outside'
                self.assertEqual(self.open(files, modes={'test/link': mode | 0o777}), 0)

    def test_root_nested_and_wrapped_layout_strip_exact_prefix(self):
        original = fixtures.port()
        for prefix in ('', 'test/', 'wrapper/download/test/'):
            with self.subTest(prefix=prefix):
                files = {prefix + name[5:]: data for name, data in original.items()
                         if name.startswith('test/')}
                files['LICENSE.txt'] = b'root legal notice'
                self.assertTrue(self.ready(files)['ready'])
                with self.package() as archive:
                    names = archive.namelist()
                    self.assertIn('runtime/port/config.json', names)
                    self.assertEqual(archive.read('runtime/port/Data.rsdk'), original['test/Data.rsdk'])
                    self.assertEqual(len(names), len(set(names)))
                    if prefix:
                        self.assertIn('runtime/upstream/LICENSE.txt', names)

    def test_null_and_scalar_metadata_and_launch_do_not_crash(self):
        metadata = [b'null', b'[]', b'true', b'1', b'"config"']
        for value in (None, False, 0, 'native', []):
            config = json.loads(fixtures.port()['test/config.json'])
            config['launch'] = value
            metadata.append(json.dumps(config).encode())
        config = json.loads(fixtures.port()['test/config.json'])
        del config['launch']
        metadata.append(json.dumps(config).encode())
        for value in metadata:
            with self.subTest(metadata=value[:120]):
                files = fixtures.port()
                files['test/config.json'] = value
                self.assertEqual(self.open(files), 0)

    def test_duplicate_keys_nul_depth_and_trailing_json_rejected(self):
        config = fixtures.port()['test/config.json']
        payloads = [config[:-1] + b',"id":"second"}', config + b' {}', config + b'\0',
                    b'[' * 33 + b'0' + b']' * 33,
                    config.replace(b'Test Game', b'Test\\u0000Game')]
        for data in payloads:
            with self.subTest(data=data[:90]):
                files = fixtures.port()
                files['test/config.json'] = data
                self.assertEqual(self.open(files), 0)

    def test_native_args_schema_is_not_ignored(self):
        for args in (None, 'sh -c x', [None], [1], ['a\nb'], ['x'] * 129, ['x' * 1025]):
            with self.subTest(args=str(args)[:90]):
                self.assert_blocked(native(args=args))

    def test_valid_native_literal_args_remain_literal_metadata(self):
        args = ['--windowed', '', 'literal ; $(never-execute)']
        files = native(args=args)
        self.assertTrue(self.ready(files)['ready'])
        with self.package() as archive:
            config = json.loads(archive.read('runtime/port/config.json'))
            self.assertEqual(config['launch']['args'], args)

    def test_nonstring_or_empty_content_is_rejected(self):
        for content in (None, False, 0, [], {}, ''):
            with self.subTest(content=content):
                self.assert_blocked(configured(launch=dict(type='libretro', binary='core.so', content=content)))

    def test_required_sets_missing_or_empty_alternative_rejected(self):
        files = fixtures.port()
        config = json.loads(files['test/config.json'])
        del config['required_file_sets']
        files['test/config.json'] = json.dumps(config).encode()
        with self.subTest(case='missing required_file_sets'):
            self.assert_blocked(files)
        with self.subTest(case='empty required alternative'):
            self.assert_blocked(configured(required_file_sets=[[]]))

    def test_required_sets_null_and_malformed_rejected(self):
        for sets in (None, {}, ['Data.rsdk'], [[None]], [[1]], [['../bad']]):
            with self.subTest(sets=sets):
                self.assert_blocked(configured(required_file_sets=sets))

    def test_schema_float_does_not_pass_integer_runtime_contract(self):
        self.assert_blocked(configured(schema_version=1.0))

    def test_required_metadata_paths_must_be_files_not_directories(self):
        for name in ('test/run.sh', 'test/Data.rsdk'):
            with self.subTest(name=name):
                files = fixtures.port()
                del files[name]
                files[name + '/'] = b''
                self.assert_blocked(files)

    def test_required_set_directory_cannot_satisfy_data(self):
        files = configured(required_file_sets=[['needed']])
        files['test/needed/'] = b''
        self.assert_blocked(files)

    def test_setup_script_source_and_redirection_require_recipe(self):
        commands = [b'. ./setup.sh\n', b'printf init > generated.cfg\n',
                    b'touch generated.cfg\n']
        for profile in ('libretro',):
            for command in commands:
                with self.subTest(profile=profile, command=command):
                    files = native() if profile == 'native' else fixtures.port()
                    files['test/run.sh'] = files['test/run.sh'].replace(b'#!/bin/sh\n',
                                                                      b'#!/bin/sh\n' + command)
                    files['test/setup.sh'] = b'#!/bin/sh\nprintf prepared > generated.cfg\n'
                    self.assert_blocked(files)

    def test_libretro_noncanonical_assignment_or_arguments_require_recipe(self):
        original = fixtures.port()['test/run.sh']
        scripts = [original.replace(b'set -eu\n', b'set -eu\nREQUIRED_MODE=1\n'),
                   original.replace(b'set -eu\n', b'set -eu\nexport REQUIRED_MODE=1\n'),
                   original.rstrip(b'\n') + b' --required-engine-option\n',
                   original.replace(b'exec ', b'REQUIRED_MODE=1 exec ')]
        for script in scripts:
            with self.subTest(script=script):
                files = fixtures.port()
                files['test/run.sh'] = script
                self.assert_blocked(files)

    def test_nonempty_libretro_metadata_args_cannot_be_silently_discarded(self):
        files = configured(launch=dict(type='libretro', binary='core.so', content='Data.rsdk',
                                       args=['--required-engine-option']))
        self.assert_blocked(files)

    def test_comment_only_blacklist_hit_does_not_block(self):
        files = fixtures.port()
        files['test/run.sh'] = files['test/run.sh'].replace(
            b'#!/bin/sh\n', b'#!/bin/sh\n# Do not use curl for this offline port\n')
        self.assertTrue(self.ready(files)['ready'])

    def test_undefined_retro_symbols_rejected(self):
        files = fixtures.port()
        files['test/core.so'] = symbol_variant(defined=False)
        self.assert_blocked(files)

    def test_local_retro_symbols_are_not_exports(self):
        files = fixtures.port()
        files['test/core.so'] = symbol_variant(binding=0)
        self.assert_blocked(files)

    def test_hidden_retro_symbols_are_not_exports(self):
        files = fixtures.port()
        files['test/core.so'] = symbol_variant(visibility=2)
        self.assert_blocked(files)

    def test_object_retro_symbols_are_not_callable_api(self):
        files = fixtures.port()
        files['test/core.so'] = symbol_variant(kind=1)
        self.assert_blocked(files)

    def test_wrong_machine_flags_and_mixed_libc_rejected(self):
        for binary in (fixtures.elf(machine=40), fixtures.elf(flags=0x70001207),
                       fixtures.elf(flags=0x70001407), fixtures.elf(('libc.so', 'libc.so.6'))):
            with self.subTest(header=binary[:52].hex()):
                files = fixtures.port()
                files['test/core.so'] = binary
                self.assert_blocked(files)

    def test_musl_profile_selects_matching_frontend(self):
        files = fixtures.port()
        files['test/core.so'] = fixtures.elf(('libc.so',))
        self.assertEqual(self.open(files), 1)
        self.attach_runtime({'musl/lilretro': fixtures.elf(('libc.so',), api=False)})
        self.assertTrue(self.engine.report()['ports'][0]['ready'])
        with self.package() as archive:
            adapter = json.loads(archive.read('adapter.json'))
            self.assertEqual(adapter['runtime_profile'], 'musl')
            self.assertEqual(archive.read('runtime/frontend/lilretro'), fixtures.elf(('libc.so',), api=False))

    def test_transitive_missing_and_cross_abi_dependency_blocked(self):
        for dependency in (fixtures.elf(('libabsent.so', 'libc.so.6'), api=False),
                           fixtures.elf(('libc.so',), api=False)):
            with self.subTest(dependency=hashlib.sha256(dependency).hexdigest()):
                files = fixtures.port()
                files['test/core.so'] = fixtures.elf(('libcustom.so', 'libc.so.6'))
                self.assertEqual(self.open(files), 1)
                self.attach_runtime({'glibc/lilretro': fixtures.elf(api=False),
                                     'glibc/lib/libcustom.so': dependency})
                self.assertFalse(self.engine.report()['ports'][0]['ready'])
                self.assertEqual(self.engine.convert(), 0)

    def test_dependency_cycle_is_bounded_and_unused_pack_entries_excluded(self):
        files = fixtures.port()
        files['test/core.so'] = fixtures.elf(('libone.so', 'libc.so.6'))
        self.assertEqual(self.open(files), 1)
        self.attach_runtime({'glibc/lilretro': fixtures.elf(api=False),
                             'glibc/lib/libone.so': fixtures.elf(('libtwo.so', 'libc.so.6'), api=False),
                             'glibc/lib/libtwo.so': fixtures.elf(('libone.so', 'libc.so.6'), api=False),
                             'glibc/lib/unused.so': fixtures.elf(api=False)})
        self.assertTrue(self.engine.report()['ports'][0]['ready'])
        with self.package() as archive:
            self.assertIn('runtime/lib/libtwo.so', archive.namelist())
            self.assertNotIn('runtime/lib/unused.so', archive.namelist())

    def test_loader_dynamic_dependencies_cannot_hide_behind_sections(self):
        files = fixtures.port()
        files['test/core.so'] = program_dynamic()
        self.assert_blocked(files)

    def test_frontend_interpreter_must_match_selected_libc(self):
        self.assertEqual(self.open(fixtures.port()), 1)
        self.attach_runtime({'glibc/lilretro': program_dynamic(('libc.so.6',),
                            interpreter='/lib/ld-musl-mipsel.so.1')})
        self.assertFalse(self.engine.report()['ports'][0]['ready'])
        self.assertEqual(self.engine.convert(), 0)

    def test_elf_header_version_is_checked(self):
        files = fixtures.port()
        out = bytearray(files['test/core.so'])
        struct.pack_into('<I', out, 20, 0)
        files['test/core.so'] = bytes(out)
        self.assert_blocked(files)

    def test_libs_directory_resolution_matches_runtime_search_path(self):
        files = fixtures.port()
        files['test/core.so'] = fixtures.elf(('libcustom.so', 'libc.so.6'))
        files['test/libs/libcustom.so'] = fixtures.elf(api=False)
        self.assertTrue(self.ready(files)['ready'])
        with self.package() as archive:
            loadable = ('runtime/lib/libcustom.so', 'runtime/port/lib/libcustom.so',
                        'runtime/port/libcustom.so')
            self.assertTrue(any(name in archive.namelist() for name in loadable),
                            'Resolved libs/ provider is not in preflight.cpp LD_LIBRARY_PATH')

    def test_native_pack_dependencies_are_actually_emitted(self):
        files = native()
        files['test/core.so'] = fixtures.elf(('libcustom.so', 'libc.so.6'), api=False)
        self.assertEqual(self.open(files), 1)
        self.attach_runtime({'glibc/lib/libcustom.so': fixtures.elf(api=False)})
        self.assertTrue(self.engine.report()['ports'][0]['ready'])
        with self.package() as archive:
            self.assertIn('runtime/lib/libcustom.so', archive.namelist(),
                          'Native graph resolves against pack, but pack is only emitted for libretro')

    def test_generated_and_preserved_executable_file_modes(self):
        files = fixtures.port()
        modes = {'test/core.so': stat.S_IFREG | 0o755,
                 'test/run.sh': stat.S_IFREG | 0o755}
        self.assertEqual(self.open(files, modes=modes), 1)
        self.attach_runtime()
        with self.package() as archive:
            observed = {name: oct(archive.getinfo(name).external_attr >> 16) for name in
                        ('test.sh', 'runtime/frontend/lilretro', 'runtime/port/core.so', 'runtime/port/run.sh')}
            for name in observed:
                with self.subTest(name=name, modes=observed):
                    info = archive.getinfo(name)
                    self.assertTrue((info.external_attr >> 16) & 0o111,
                                    'ZIP has no executable mode: ' + json.dumps(observed))


class WasmSecurityTests(SecurityTests):
    backend = WasmEngine


FINDINGS = [
    dict(id='stored-zip-output', severity='P1', file='src/converter.c', lines=[307, 308],
         tests=['valid_stored_and_mixed'],
         issue='Valid stored ZIP entries fail conversion: nonzero uncomp_size is passed without the compressed-data flag, rejected by miniz_zip.c:3319.',
         remedy='For stored bytes use the uncompressed writer API with uncomp_size=0 and level=0, or a correct raw-entry copy API; retain CRC verification.'),
    dict(id='escaped-json-nul', severity='P1', file='src/converter.c', lines=[85, 90],
         tests=['duplicate_keys_nul_depth'],
         issue='Quoted backslash handling continues before the escaped-NUL check, so \\u0000 is accepted and cJSON strings can be silently truncated.',
         remedy='Reject decoded NUL in keys and values without confusing escaped backslashes; test ID, path, args and key collisions against runtime parsing.'),
    dict(id='metadata-contract', severity='P1', file='src/converter.c', lines=[252, 256, 270],
         tests=['native_args_schema', 'nonstring_or_empty_content', 'required_sets_missing', 'schema_float',
                'nonempty_libretro_metadata_args'],
         issue='C readiness accepts metadata rejected by TreeFrogCatalog::parse on first launch.',
         remedy='Validate launch.args types/count/bytes, explicit content types, integer schema, and required nonempty data alternatives against the existing catalog contract.'),
    dict(id='setup-semantics', severity='P1', file='src/converter.c', lines=[289, 292, 337],
         tests=['setup_script_source', 'comment_only_blacklist', 'libretro_noncanonical_assignment'],
         issue='Sourcing setup scripts, touch, and output redirection bypass side-effect checks; libretro typed launch skips their semantics. Comments also cause false positives.',
         remedy='Require a typed, validated adaptation recipe for setup/env/options; do not equate a substring denylist with semantic safety.'),
    dict(id='elf-loader-view', severity='P1', file='src/converter.c', lines=[146, 155, 164],
         tests=['loader_dynamic_dependencies', 'frontend_interpreter', 'elf_header_version'],
         issue='Dependencies/ABI are read from sections instead of the loader PT_DYNAMIC/PT_INTERP view, and e_version is unchecked.',
         remedy='Parse mapped program-header metadata, check interpreter/ABI consistency and required ELF header fields, reject conflicting loader/section descriptions.'),
    dict(id='elf-export-callability', severity='P1', file='src/converter.c', lines=[172, 175],
         tests=['local_retro', 'hidden_retro', 'object_retro'],
         issue='Defined local, hidden, and data symbols count as callable Libretro exports.',
         remedy='Require exported global/weak callable symbols with default/protected visibility and an appropriate defined executable location.'),
    dict(id='native-runtime-closure', severity='P1', file='src/converter.c', lines=[189, 204, 323],
         tests=['native_pack_dependencies'],
         issue='Native dependency graph can resolve a runtime-pack library but tf_convert emits runtime-pack libraries only inside the libretro branch.',
         remedy='Emit every selected bundled provider for both profiles, or block unsupported native pack resolution.'),
    dict(id='library-search-layout', severity='P1', file='src/converter.c', lines=[191, 325],
         tests=['libs_directory_resolution'],
         issue='Resolver accepts port libs/ providers, but runtime preflight only searches runtime/lib, port/lib, and port root.',
         remedy='Normalize all resolved private providers to runtime/lib or align the exact typed runtime search path.'),
    dict(id='zip-executable-modes', severity='P1', file='src/converter.c', lines=[298, 307, 343],
         tests=['generated_and_preserved_executable'],
         issue='Miniz add_mem/add_mem_ex output has no executable external attributes for frontend, generated launcher, original engine or run.sh.',
         remedy='Emit explicit regular-file modes for generated entries and preserve safe input executable bits; verify the installed frontend is executable.'),
    dict(id='metadata-file-type', severity='P1', file='src/converter.c', lines=[262, 264, 277],
         tests=['required_metadata_paths', 'required_set_directory'],
         issue='Directory entries satisfy run.sh, content and required-file lookups, but are dropped from the generated ZIP.',
         remedy='Require non-directory regular-file entries when resolving all metadata-declared files.'),
]


def fingerprint(relative):
    path = ROOT / relative
    return dict(path=relative, bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest()) if path.exists() else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wasm', action='store_true', help='Also exercise the shipped WASM through Node')
    parser.add_argument('--audit', action='store_true', help='Write only _build/audit.json')
    parser.add_argument('--brief', action='store_true', help='Keep tracebacks in audit.json instead of terminal output')
    args = parser.parse_args()
    before = [fingerprint(path) for path in ('src/converter.c', 'build.py', '_build/libtfconvert.so',
              'site/dist/converter.js', 'site/dist/converter.wasm', 'site/dist/worker.js')]
    loader = unittest.TestLoader()
    suite = unittest.TestSuite([loader.loadTestsFromTestCase(fixtures.CoreTests),
                               loader.loadTestsFromTestCase(SecurityTests)])
    if args.wasm:
        suite.addTests(loader.loadTestsFromTestCase(WasmSecurityTests))
    result = unittest.TextTestRunner(verbosity=2, stream=io.StringIO() if args.brief else None).run(suite)
    failures = [dict(test=str(test), traceback=detail) for test, detail in result.failures]
    errors = [dict(test=str(test), traceback=detail) for test, detail in result.errors]
    active = []
    for finding in FINDINGS:
        matches = [item['test'] for item in failures + errors
                   if any(token in item['test'] for token in finding['tests'])]
        if matches:
            active.append(dict(finding, reproduced_by=matches))
    audit = dict(timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                 scope='Synthetic static conversion only; no engine, launcher, game, card or VM execution.',
                 command='python3 -B tools/treefrog-converter/tests/security_qa.py --audit' + (' --wasm' if args.wasm else ''),
                 tests_run=result.testsRun, failure_assertions=len(failures), errors=len(errors),
                 skipped=len(result.skipped), passed=result.wasSuccessful(),
                 artifacts_before=before, artifacts_after=[fingerprint(item['path']) for item in before if item],
                 findings=active, failures=failures, error_details=errors,
                 gaps=['No physical-device or gameplay validation.',
                       'No build.py invocation: rebuilding/writing shared artifacts is outside audit ownership.',
                       'No ASAN rebuild, allocation-failure injection, broad fuzzing or large archive allocation.',
                       'No full installer invocation; executable-mode finding is direct ZIP metadata evidence.',
                       'No external downloads, card writes, apt or full VM.'])
    if args.audit:
        (ROOT / '_build/audit.json').write_text(json.dumps(audit, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: audit[key] for key in ('tests_run', 'failure_assertions', 'errors', 'passed')}, indent=2))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    raise SystemExit(main())
