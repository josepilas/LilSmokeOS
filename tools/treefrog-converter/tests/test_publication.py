#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Publication path, content, completeness, manifest and determinism checks."""
import io
import json
from pathlib import Path
import sys
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import source_release as release


class PublicationTests(unittest.TestCase):
    def test_safe_names(self):
        for name in ('../escape', '/absolute', 'x/../y', 'C:bad', 'x\\y', '.git/config', '.openai/id', '.env.local'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.safe_name(name)

    def test_content_signatures(self):
        for content in (b'/ro' + b'ot/private', b'/mnt/' + b'c/private', b'C:/' + b'Users/person',
                        b'ghp_' + b'x' * 36, b'-----BEGIN ' + b'PRIVATE KEY-----'):
            with self.subTest(content=content), self.assertRaises(ValueError):
                release.audit({'test.txt': content})

    def test_nested_content_signatures(self):
        data = release.zip_bytes({'test.txt': b'/ro' + b'ot/private'})
        with self.assertRaises(ValueError):
            release.audit({'payload.zip': data})

    def test_case_collisions(self):
        with self.assertRaises(ValueError):
            release.audit({'A': b'', 'a': b''})

    def test_assets_rejected(self):
        for name in ('input.lilpck', 'capture.png', 'firmware.img'):
            with self.assertRaises(ValueError):
                release.audit({name: b''})

    def test_zip_deterministic_modes(self):
        one = release.zip_bytes({'b': b'2', 'a': b'1'}, ('a',))
        self.assertEqual(one, release.zip_bytes({'a': b'1', 'b': b'2'}, ('a',)))
        with zipfile.ZipFile(io.BytesIO(one)) as archive:
            self.assertEqual(archive.namelist(), ['a', 'b'])
            self.assertEqual(archive.getinfo('a').external_attr >> 16, 0o100755)
            self.assertEqual(archive.getinfo('b').date_time, release.FIXED_TIME)

    def test_runtime_completeness(self):
        entries = release.runtime_sources()
        root = release.HERE / 'runtime'
        actual = {p.relative_to(root).as_posix() for p in root.iterdir() if p.suffix in ('.c', '.h', '.py', '.md')}
        actual.update(p.relative_to(root).as_posix() for p in (root / 'tests').iterdir() if p.suffix in ('.c', '.h', '.py'))
        self.assertTrue(actual <= entries.keys(), actual - entries.keys())

    def test_source_inventory(self):
        entries = release.source_entries(release.HERE / 'site/dist')
        manifest = json.loads(entries.pop('SHA256SUMS.json'))
        self.assertEqual(manifest, release.inventory(entries))
        self.assertNotIn('tests/full_vm.py', entries)
        self.assertNotIn('tests/vm_pad.c', entries)
        self.assertIn('runtime/gamepad-state.h', entries)
        self.assertIn('runtime/vendor/gamepad-MIT.txt', entries)
        release.audit(entries)

    def test_non_elf_rejected(self):
        with self.assertRaises(ValueError):
            release.load_image(b'not an elf')


if __name__ == '__main__':
    unittest.main(verbosity=2)
