# SPDX-License-Identifier: MPL-2.0
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
import os
import subprocess
import shlex

from native_build import ASSET_TOOLS, TOOL_BUILD_CHECK, HEXDUMP_RECIPE, BIN2C_RECIPE, NativeBuild, prepare_asset_tools, prepare_engine_makefile, copy_buffered, extract_source, main
from cook import Cancelled
from unittest.mock import patch
from test_cook import executable


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_cached_only_verifies_without_preparation_or_rom_access(self):
        output=io.StringIO()
        with patch('sys.argv',['native_build.py','--app',str(self.root),'--cached-only']), \
                patch('native_build.NativeBuild') as builder, patch('native_build.Cache') as cache, \
                patch('native_build.signal.signal'), patch('sys.stdout',output):
            cache.return_value.current.return_value=self.root/'cache/game'
            self.assertEqual(main(),0)
            cache.return_value.prepare.assert_not_called()
            builder.return_value.build.assert_not_called()
        import json
        self.assertEqual(json.loads(output.getvalue()),dict(ready=str(self.root/'cache/game')))

    def test_missing_cached_game_never_starts_build_in_cached_only_mode(self):
        with patch('sys.argv',['native_build.py','--app',str(self.root),'--cached-only']), \
                patch('native_build.NativeBuild') as builder, patch('native_build.Cache') as cache, \
                patch('native_build.signal.signal'):
            cache.return_value.current.return_value=None
            self.assertEqual(main(),3)
            cache.return_value.prepare.assert_not_called()
            builder.return_value.build.assert_not_called()

    def test_real_compile_command_limits_macro_tracking_without_lowering_optimization(self):
        stage = self.root / 'stage'
        work = stage / 'source'
        work.mkdir(parents=True)
        builder = NativeBuild.__new__(NativeBuild)
        builder.runtime = self.root / 'runtime'
        class PlanCaptured(Exception):
            pass
        with patch('native_build.prepare_engine_makefile'), \
                patch('native_build.prepare_build_clock', return_value=stage / 'clock.mk'), \
                patch('native_build.regular'), patch('native_build.copy_buffered'), \
                patch('native_build.run_stage', side_effect=PlanCaptured) as run:
            with self.assertRaises(PlanCaptured):
                builder.compile(stage, work, 123, lambda *_: None, lambda: False)
        command = run.call_args.args[0]
        self.assertEqual(command[-1], '-n')
        self.assertIn('-j1', command)
        flags = shlex.split(next(arg[7:] for arg in command if arg.startswith('CFLAGS=')))
        self.assertIn('-ftrack-macro-expansion=0', flags)
        self.assertIn('-O2', flags)
        self.assertIn('-fno-fast-math', flags)
        self.assertIn('-mips32r2', flags)
        self.assertNotIn('-O0', flags)

    def archive(self, entries):
        path = self.root / 'source.tar.gz'
        with tarfile.open(path, 'w:gz') as output:
            for name, data, kind in entries:
                item = tarfile.TarInfo(name)
                item.type = kind
                item.size = len(data) if kind == tarfile.REGTYPE else 0
                if kind == tarfile.SYMTYPE:
                    item.linkname = '/etc/passwd'
                output.addfile(item, io.BytesIO(data) if item.isfile() else None)
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_source_extracts_to_transaction_only(self):
        path, digest = self.archive([('src/test.c', b'/* fixture */', tarfile.REGTYPE)])
        work = self.root / 'work'
        extract_source(path, work, digest)
        self.assertEqual((work / 'src/test.c').read_bytes(), b'/* fixture */')

    def test_wrong_source_checksum(self):
        path, _ = self.archive([('test.c', b'', tarfile.REGTYPE)])
        with self.assertRaisesRegex(ValueError, 'checksum'):
            extract_source(path, self.root / 'work', '0' * 64)
        self.assertFalse((self.root / 'work').exists())

    def test_path_traversal(self):
        path, digest = self.archive([('../outside', b'bad', tarfile.REGTYPE)])
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            extract_source(path, self.root / 'work', digest)
        self.assertFalse((self.root / 'outside').exists())

    def test_links_rejected(self):
        path, digest = self.archive([('linked', b'', tarfile.SYMTYPE)])
        with self.assertRaisesRegex(ValueError, 'links'):
            extract_source(path, self.root / 'work', digest)

    def test_case_collisions_rejected(self):
        path, digest = self.archive([('One.c', b'a', tarfile.REGTYPE), ('one.c', b'b', tarfile.REGTYPE)])
        with self.assertRaisesRegex(ValueError, 'duplicates'):
            extract_source(path, self.root / 'work', digest)

    def test_unpack_reports_real_bytes_and_completion(self):
        data = b'fixture' * 1000
        path, digest = self.archive([('src/test.c', data, tarfile.REGTYPE)])
        updates = []
        extract_source(path, self.root / 'work', digest, progress=lambda *args: updates.append(args))
        self.assertEqual(updates[0], ('Unpacking engine', 10, 0))
        self.assertEqual(updates[-1], ('Unpacking engine', 20, len(data)))

    def test_case_colliding_parent_directories_rejected(self):
        path, digest = self.archive([('One/a.c', b'a', tarfile.REGTYPE), ('one/b.c', b'b', tarfile.REGTYPE)])
        with self.assertRaisesRegex(ValueError, 'case-colliding'):
            extract_source(path, self.root / 'work', digest)

    def test_file_cannot_become_parent_directory(self):
        path, digest = self.archive([('parent', b'a', tarfile.REGTYPE), ('parent/child', b'b', tarfile.REGTYPE)])
        with self.assertRaises(FileExistsError):
            extract_source(path, self.root / 'work', digest)

    def test_unpack_cancels_within_large_file(self):
        path, digest = self.archive([('large', b'a' * 400000, tarfile.REGTYPE)])
        checks = iter((False, False, True))
        with self.assertRaises(Cancelled):
            extract_source(path, self.root / 'work', digest, cancel=lambda: next(checks))
        self.assertEqual((self.root / 'work/large').stat().st_size, 128 * 1024)

    def test_buffered_copy_never_calls_sendfile(self):
        source = self.root / 'input'
        data = bytes(range(256)) * 1024
        source.write_bytes(data)
        with patch('os.sendfile', side_effect=AssertionError('sendfile must not be used')):
            copy_buffered(source, self.root / 'output')
        self.assertEqual((self.root / 'output').read_bytes(), data)

    def test_buffered_copy_cancels_between_blocks(self):
        source = self.root / 'input'
        source.write_bytes(b'a' * 400000)
        checks = iter((False, True))
        with self.assertRaises(Cancelled):
            copy_buffered(source, self.root / 'output', lambda: next(checks))
        self.assertEqual((self.root / 'output').stat().st_size, 128 * 1024)

    def test_prebuilt_helpers_are_not_rebuilt_when_sources_are_newer(self):
        work = self.root / 'work'
        tools = work / 'tools'
        tools.mkdir(parents=True)
        for name in ASSET_TOOLS:
            binary = tools / name
            binary.write_bytes(executable())
            os.utime(binary, (1, 1))
            (tools / (name + '.c')).write_text('newer source fixture')
        shim = prepare_asset_tools(self.root, work) / 'make'
        subprocess.run([str(shim), '-s', '-C', 'tools/', *ASSET_TOOLS], check=True)
        self.assertEqual(subprocess.run([str(shim), 'all'], capture_output=True).returncode, 77)

    def test_invalid_bundled_helper_is_rejected(self):
        tools = self.root / 'tools'
        tools.mkdir()
        (tools / ASSET_TOOLS[0]).write_bytes(b'not a MIPS executable')
        with self.assertRaises(ValueError):
            prepare_asset_tools(self.root, self.root)

    def engine_fixture(self, check=TOOL_BUILD_CHECK):
        tools = self.root / 'tools'
        tools.mkdir()
        for name in ASSET_TOOLS:
            binary = tools / name
            binary.write_bytes(executable())
            os.utime(binary, (1, 1))
            (tools / (name + '.c')).write_text('newer source fixture')
        (tools / 'Makefile').write_text('all:\n\t@echo UNEXPECTED_REBUILD >&2\n\t@false\n')
        (self.root / 'Makefile').write_text(check + '\nifeq ($(DUMMY),FAIL)\n$(error Failed to build tools)\nendif\nall:\n\t@echo GAME_BUILD\n')

    def test_engine_uses_real_make_without_rebuilding_verified_helpers(self):
        self.engine_fixture()
        prepare_engine_makefile(self.root)
        result = subprocess.run(['make', '-f', 'Makefile'], cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('GAME_BUILD', result.stdout)
        self.assertNotIn('UNEXPECTED_REBUILD', result.stderr)

    def test_engine_makefile_layout_must_match_exactly_once(self):
        for check in ('DUMMY := unknown', TOOL_BUILD_CHECK + '\n' + TOOL_BUILD_CHECK):
            with self.subTest(check=check):
                self.engine_fixture(check)
                before = (self.root / 'Makefile').read_bytes()
                with self.assertRaisesRegex(ValueError, 'Unexpected bundled'):
                    prepare_engine_makefile(self.root)
                self.assertEqual((self.root / 'Makefile').read_bytes(), before)
                import shutil
                shutil.rmtree(self.root / 'tools')

    def test_engine_rejects_invalid_helper_before_patching(self):
        self.engine_fixture()
        (self.root / 'tools' / ASSET_TOOLS[0]).write_bytes(b'bad ELF')
        before = (self.root / 'Makefile').read_bytes()
        with self.assertRaises(ValueError):
            prepare_engine_makefile(self.root)
        self.assertEqual((self.root / 'Makefile').read_bytes(), before)

    def test_verified_makefile_and_fast_binary_rules_are_idempotent(self):
        self.engine_fixture()
        with (self.root / 'Makefile').open('a') as output:
            output.write(HEXDUMP_RECIPE + '\n' + HEXDUMP_RECIPE + '\n')
        prepare_engine_makefile(self.root, fast_bin2c=True)
        first = (self.root / 'Makefile').read_bytes()
        self.assertEqual(first.decode().count(BIN2C_RECIPE), 2)
        prepare_engine_makefile(self.root, fast_bin2c=True)
        self.assertEqual((self.root / 'Makefile').read_bytes(), first)

    def test_fast_binary_patch_rejects_an_unknown_recipe_layout(self):
        self.engine_fixture()
        before = (self.root / 'Makefile').read_bytes()
        with self.assertRaisesRegex(ValueError, 'binary-conversion'):
            prepare_engine_makefile(self.root, fast_bin2c=True)
        self.assertEqual((self.root / 'Makefile').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
