# SPDX-License-Identifier: MPL-2.0
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from independent_objects import (
    BUILD, ELIGIBLE, LIMIT, SCHEMA, SOURCE_INPUTS, install_objects,
    normalized_flags, record, safe_name, sha, verify_object,
)
from prebuild_independent import SOURCE_SHA256, extract_inputs, parse_dependencies, production_flags
from audit_independent_candidates import CANDIDATES, classify_dependencies


def object_fixture():
    data = bytearray(156)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHI', data, 16, 1, 8, 1)
    struct.pack_into('<I', data, 32, 52)
    struct.pack_into('<I', data, 36, 0x70001007)
    struct.pack_into('<HH', data, 46, 40, 2)
    struct.pack_into('<10I', data, 92, 0, 0x7000002A, 0, 0, 132, 24, 0, 0, 8, 24)
    struct.pack_into('<H6B4I', data, 132, 0, 32, 2, 1, 1, 0, 1, 0, 0, 0, 0)
    return bytes(data)


class IndependentObjectsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.bundle, self.work, self.runtime = (self.root / x for x in ('bundle', 'work', 'Runtime With Spaces'))
        for path in (self.bundle, self.work, self.runtime):
            path.mkdir()
        self.flags = ['-O2', '-mips32r2', '-isystem', str(self.runtime / 'sysroot/usr/include')]
        for name in SOURCE_INPUTS:
            self.write(self.work / name, b'MPL test source\n')
        self.header = 'sysroot/usr/include/test.h'
        self.write(self.runtime / self.header, b'system fixture\n')
        self.tool = 'compiler/wrappers/cc'
        self.write(self.runtime / self.tool, b'tool fixture\n')
        self.manifest = dict(schema=SCHEMA, source_sha256='a' * 64,
                             flags=normalized_flags(self.flags, self.runtime),
                             toolchain=[record(self.runtime / self.tool, self.tool)],
                             audit=dict(distribution_scope='MPL-2.0 LilSmoke adaptations only'), objects=[])
        for source, (leaf, extra) in ELIGIBLE.items():
            path = self.bundle / 'objects' / leaf
            self.write(path, object_fixture())
            deps = []
            for name in (source, 'lilsmoke/LICENSE', 'lilsmoke/public.mk', 'lilsmoke/m8-audio.h'):
                deps.append(dict(scope='source', **record(self.work / name, name)))
            deps.append(dict(scope='runtime', **record(self.runtime / self.header, self.header)))
            self.manifest['objects'].append(dict(source=source, target=BUILD + leaf,
                extra_flags=extra, object=record(path, 'objects/' + leaf), prerequisites=deps))

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def install(self, **kwargs):
        data = (json.dumps(self.manifest, sort_keys=True) + '\n').encode()
        self.write(self.bundle / 'manifest.json', data)
        options = dict(expected_manifest_sha256=sha(data), source_sha256='a' * 64, flags=self.flags)
        options.update(kwargs)
        return install_objects(self.bundle, self.work, self.runtime, **options)

    def assert_no_outputs(self):
        self.assertFalse((self.work / 'build').exists())

    def test_installs_exact_objects_and_make_dependencies(self):
        result = self.install(epoch=2000000000)
        self.assertEqual(len(result['installed']), 2)
        self.assertEqual(result['skipped'], {})
        self.assertEqual(result['makefiles'], [target[:-2] + '.d' for target in result['installed']])
        for target in result['installed']:
            path = self.work / target
            self.assertEqual(path.read_bytes(), object_fixture())
            deps = path.with_suffix('.d').read_text()
            self.assertIn('Runtime\\ With\\ Spaces', deps)
            self.assertIn('lilsmoke/m8-audio.h', deps)
            self.assertGreaterEqual(path.stat().st_mtime, 2000000000)

    def test_changed_source_skips_only_affected_object(self):
        source = next(iter(ELIGIBLE))
        (self.work / source).write_bytes(b'new source\n')
        result = self.install()
        self.assertEqual(len(result['installed']), 1)
        self.assertIn(BUILD + ELIGIBLE[source][0], result['skipped'])

    def test_changed_shared_header_skips_all(self):
        (self.work / 'lilsmoke/m8-audio.h').write_bytes(b'changed\n')
        result = self.install()
        self.assertEqual(result['installed'], [])
        self.assertEqual(len(result['skipped']), 2)
        self.assert_no_outputs()

    def test_missing_prerequisite_skips(self):
        (self.runtime / self.header).unlink()
        self.assertEqual(len(self.install()['skipped']), 2)
        self.assert_no_outputs()

    def test_empty_system_header_is_valid(self):
        path = self.runtime / self.header
        path.write_bytes(b'')
        item = dict(scope='runtime', **record(path, self.header, allow_empty=True))
        for entry in self.manifest['objects']:
            entry['prerequisites'][-1] = item
        self.assertEqual(len(self.install()['installed']), 2)

    def test_changed_header_invalidates_existing_objects_even_with_preserved_mtime(self):
        installed = self.install()['installed']
        path = self.work / 'lilsmoke/m8-audio.h'
        old = path.stat()
        path.write_bytes(b'changed bytes\n')
        os.utime(path, ns=(old.st_atime_ns, old.st_mtime_ns))
        result = self.install()
        self.assertEqual(len(result['skipped']), 2)
        self.assertEqual(len(result['invalidated']), 4)
        self.assertTrue(all(not (self.work / target).exists() for target in installed))

    def test_changed_tool_rejects_all(self):
        (self.runtime / self.tool).write_bytes(b'new tool\n')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.install()
        self.assert_no_outputs()

    def test_changed_system_header_skips_all(self):
        (self.runtime / self.header).write_bytes(b'new system header\n')
        self.assertEqual(len(self.install()['skipped']), 2)
        self.assert_no_outputs()

    def test_corrupt_second_object_writes_nothing(self):
        leaf = self.manifest['objects'][1]['object']['path']
        (self.bundle / leaf).write_bytes(b'corruption')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.install()
        self.assert_no_outputs()

    def test_untrusted_manifest_rejected(self):
        with self.assertRaisesRegex(ValueError, 'manifest digest'):
            self.install(expected_manifest_sha256='0' * 64)
        self.assert_no_outputs()

    def test_wrong_contract_rejected(self):
        for option in (dict(source_sha256='b' * 64), dict(flags=['-O0'])):
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, 'contract'):
                self.install(**option)
        self.assert_no_outputs()

    def test_size_limit_and_size_mismatch_rejected(self):
        item = self.manifest['objects'][0]['object']
        for size in (-1, 0, LIMIT + 1, True, '156', 157):
            item['bytes'] = size
            with self.subTest(size=size), self.assertRaises(ValueError):
                self.install()
        self.assert_no_outputs()

    def test_bad_paths(self):
        for name in ('../x', '/x', 'a//b', 'a/./b', 'a/../b', 'a\\b', 'C:/x', 'a\nx', 'a b', ''):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_name(name)

    def test_wrong_object_path_and_target_rejected(self):
        entry = self.manifest['objects'][0]
        entry['target'] = '../outside.o'
        with self.assertRaisesRegex(ValueError, 'target'):
            self.install()
        self.assert_no_outputs()

    def test_renderer_or_generated_dependency_rejected(self):
        entry = self.manifest['objects'][0]
        for name in ('src/pc/gfx/gfx_pc.h', 'build/original-mips/generated.h', 'actors/mario/model.inc.c', 'include/PR/gbi.h'):
            item = dict(scope='source', path=name, bytes=1, sha256='a' * 64)
            entry['prerequisites'].append(item)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Unapproved source'):
                self.install()
            entry['prerequisites'].pop()
        self.assert_no_outputs()

    def test_duplicate_or_unapproved_source_rejected(self):
        self.manifest['objects'].append(self.manifest['objects'][0])
        with self.assertRaises(ValueError):
            self.install()
        self.assert_no_outputs()

    def test_missing_license_or_primary_source_rejected(self):
        entry = self.manifest['objects'][0]
        entry['prerequisites'] = [x for x in entry['prerequisites'] if x['path'] != 'lilsmoke/LICENSE']
        with self.assertRaisesRegex(ValueError, 'licence prerequisite'):
            self.install()
        self.assert_no_outputs()

    @unittest.skipUnless(os.name == 'posix', 'requires POSIX links')
    def test_dependency_symlink_rejected_not_skipped(self):
        path = self.work / 'lilsmoke/m8-audio.h'
        path.unlink()
        path.symlink_to(self.runtime / self.header)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.install()
        self.assert_no_outputs()

    @unittest.skipUnless(os.name == 'posix', 'requires POSIX links')
    def test_object_hardlink_rejected(self):
        path = self.bundle / self.manifest['objects'][0]['object']['path']
        os.link(path, self.root / 'linked')
        with self.assertRaisesRegex(ValueError, 'unshared'):
            self.install()
        self.assert_no_outputs()

    @unittest.skipUnless(os.name == 'posix', 'requires POSIX links')
    def test_destination_parent_symlink_rejected(self):
        (self.work / 'build').symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.install()
        self.assertFalse((self.root / 'original-mips').exists())

    @unittest.skipUnless(os.name == 'posix', 'requires POSIX links')
    def test_bundle_root_symlink_rejected(self):
        alias = self.root / 'alias'
        alias.symlink_to(self.bundle, target_is_directory=True)
        original = self.bundle
        self.install()
        self.bundle = alias
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.install()
        self.bundle = original

    def test_existing_destination_directory_rejected(self):
        destination = self.work / self.manifest['objects'][0]['target']
        destination.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'regular'):
            self.install()
        self.assertTrue(destination.is_dir())

    def test_cancellation_writes_nothing(self):
        with self.assertRaises(InterruptedError):
            self.install(cancel=lambda: True)
        self.assert_no_outputs()

    def test_invalid_elf_and_abi_rejected(self):
        verify_object(object_fixture())
        for offset, value in ((4, 2), (5, 2), (16, 2), (18, 62), (39, 0), (135, 1), (139, 3)):
            data = bytearray(object_fixture())
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                verify_object(bytes(data))
        with self.assertRaises(ValueError):
            verify_object(object_fixture()[:140])

    @unittest.skipUnless(shutil.which('make'), 'GNU Make required')
    def test_gnu_make_tracks_installed_dependencies_and_future_input(self):
        target = self.install()['installed'][0]
        makefile = self.work / 'Makefile'
        makefile.write_text(target + ':\n\t@false\ninclude ' + target[:-2] + '.d\n')
        command = ['make', '-s', '-q', '-f', str(makefile), target]
        self.assertEqual(subprocess.run(command, cwd=self.work).returncode, 0)
        header = self.work / 'lilsmoke/m8-audio.h'
        future = (self.work / target).stat().st_mtime + 4
        os.utime(header, (future, future))
        self.assertEqual(subprocess.run(command, cwd=self.work).returncode, 1)


class ProducerTests(unittest.TestCase):
    def test_dependencies_with_continuations_and_escaped_spaces(self):
        target, deps = parse_dependencies('out.o: src/a.c \\\n /runtime\\ with\\ spaces/a.h src/a.c\nphony.h:\n')
        self.assertEqual(target, 'out.o')
        self.assertEqual(deps, ['/runtime with spaces/a.h', 'src/a.c'])

    def test_flags_ast_does_not_execute_arbitrary_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'native_build.py').write_text('def compile():\n    flags = [evil()]\n')
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                production_flags(root)

    def test_source_archive_rejects_symlink_and_case_collision(self):
        for kind in ('link', 'collision'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / 'source.tar.gz'
                with tarfile.open(archive, 'w:gz') as handle:
                    item = tarfile.TarInfo('file.h')
                    item.size = 1
                    handle.addfile(item, io.BytesIO(b'x'))
                    item = tarfile.TarInfo('FILE.H' if kind == 'collision' else 'other.h')
                    if kind == 'link':
                        item.type, item.linkname = tarfile.SYMTYPE, '/etc/passwd'
                    handle.addfile(item)
                with patch('prebuild_independent.SOURCE_SHA256', sha(archive.read_bytes())):
                    with self.assertRaisesRegex(ValueError, 'Duplicate or linked'):
                        extract_inputs(archive, root / 'output')
                self.assertFalse((root / 'output').exists())


class GenericAuditTests(unittest.TestCase):
    def test_technical_closure_is_not_licence_clearance(self):
        deps = ['src/game/mario.c', 'include/PR/gbi.h']
        result = classify_dependencies(deps, set(deps))
        self.assertTrue(result['source_only_closure'])
        self.assertFalse(result['distribution_approved'])
        self.assertEqual(result['proprietary_header_boundary'], ['include/PR/gbi.h'])

    def test_graphics_header_is_excluded_even_if_available(self):
        deps = ['src/pc/gfx/gfx_pc.h']
        self.assertFalse(classify_dependencies(deps, set(deps))['source_only_closure'])

    def test_missing_generated_header_is_excluded(self):
        result = classify_dependencies(['build/original-mips/generated.h'], set())
        self.assertFalse(result['source_only_closure'])
        self.assertEqual(result['missing_or_generated'], ['build/original-mips/generated.h'])

    def test_source_present_data_include_is_not_generic_code(self):
        deps = ['include/trig_tables.inc.c']
        result = classify_dependencies(deps, set(deps))
        self.assertFalse(result['source_only_closure'])
        self.assertEqual(result['data_boundary'], deps)

    def test_explicit_candidates_contain_no_renderers_or_asset_units(self):
        self.assertEqual(len(CANDIDATES), len(set(CANDIDATES)))
        self.assertTrue(all(name.startswith(('src/engine/', 'src/audio/', 'src/game/', 'src/pc/')) for name in CANDIDATES))
        self.assertFalse(any('gfx/' in name or 'renderer' in name or name.endswith('.inc.c') for name in CANDIDATES))


@unittest.skipUnless(os.environ.get('SM64_INDEPENDENT_BUNDLE'), 'optional real frozen-runtime acceptance')
class RealBundleTests(unittest.TestCase):
    def test_real_source_only_install_and_make_invalidation(self):
        runtime = Path(os.environ['SM64_INDEPENDENT_RUNTIME'])
        bundle = Path(os.environ['SM64_INDEPENDENT_BUNDLE'])
        expected = os.environ['SM64_INDEPENDENT_MANIFEST_SHA256']
        out = Path(os.environ['SM64_INDEPENDENT_QA_OUT']).absolute()
        if out.parent != Path('/root') or not out.name.startswith('sm64-independent-') or out.exists():
            self.fail('Acceptance output must be a fresh /root/sm64-independent-* directory')
        out.mkdir()
        work = out / 'source'
        contents, _ = extract_inputs(runtime / 'source.tar.gz', work)
        flags = production_flags(runtime)
        options = dict(expected_manifest_sha256=expected,
                       source_sha256=SOURCE_SHA256, flags=flags)
        result = install_objects(bundle, work, runtime, **options)
        self.assertEqual(len(result['installed']), 2)
        self.assertEqual(result['skipped'], {})
        makefile = work / 'Makefile'
        text = ''.join(target + ':\n\t@exit 99\n' for target in result['installed'])
        makefile.write_text(text)
        commands = []
        for target in result['installed']:
            verify_object((work / target).read_bytes())
            command = ['make', '-s', '-q', '-f', 'Makefile']
            for dependency in result['makefiles']:
                command.extend(['-f', dependency])
            process = subprocess.run(command + [target], cwd=work, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stderr)
            commands.append(dict(target=target, make_up_to_date=True))
        header = work / 'lilsmoke/m8-audio.h'
        old = header.stat()
        header.write_bytes(b'changed prerequisite with original mtime\n')
        os.utime(header, ns=(old.st_atime_ns, old.st_mtime_ns))
        changed = install_objects(bundle, work, runtime, **options)
        self.assertEqual(len(changed['skipped']), 2)
        self.assertEqual(len(changed['invalidated']), 4)
        self.assertTrue(all(not (work / target).exists() for target in result['installed']))
        header.write_bytes(contents['lilsmoke/m8-audio.h'])
        restored = install_objects(bundle, work, runtime, **options)
        self.assertEqual(restored['installed'], result['installed'])
        manifest = json.loads((bundle / 'manifest.json').read_text())
        report = dict(passed=True, manifest_sha256=expected,
                      source_only_inputs=sorted(contents),
                      object_records=[entry['object'] for entry in manifest['objects']],
                      prerequisite_counts=[len(entry['prerequisites']) for entry in manifest['objects']],
                      commands=commands, changed_dependency_preserved_mtime_invalidated=True,
                      restored=restored, flags_unchanged=True, original_runtime_modified=False,
                      source_only_test=True, full_game_link_test=False, physical_device_test=False)
        (out / 'acceptance.json').write_text(json.dumps(report, sort_keys=True, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
