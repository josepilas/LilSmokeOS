#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import hashlib
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
import json

from native_build import NativeBuild

import prepare_performance_update as update

sys.path.insert(0, str(update.ORIGINAL))
from test_occlusion_adapter import fixture


def tar_bytes(entries):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode='w:gz') as archive:
        for name, data in entries:
            item = tarfile.TarInfo(name)
            item.size = len(data)
            item.mode = 0o644
            archive.addfile(item, io.BytesIO(data))
    return stream.getvalue()


class PackageSourceTests(unittest.TestCase):
    def rewrite(self, entries, before=None, after=None):
        original = fixture().encode()
        modified = update.transform(original.decode()).encode()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'source.tar.gz'
            result = update.rewrite_source(io.BytesIO(tar_bytes(entries)), path,
                before or hashlib.sha256(original).hexdigest(), after or hashlib.sha256(modified).hexdigest())
            with tarfile.open(path) as archive:
                values = {item.name: archive.extractfile(item).read() for item in archive}
            return result, values

    def test_only_tested_renderer_changes(self):
        result, values = self.rewrite([(update.RENDERER, fixture().encode()), ('LICENSE', b'unchanged')])
        self.assertEqual(result, [update.RENDERER])
        self.assertEqual(values['LICENSE'], b'unchanged')
        self.assertEqual(values[update.RENDERER], update.transform(fixture()).encode())

    def test_wrong_before_and_after_hash_rejected(self):
        for key in ('before', 'after'):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.rewrite([(update.RENDERER, fixture().encode())], **{key: '0' * 64})

    def test_duplicate_and_traversal_rejected(self):
        for entries in ([(update.RENDERER, fixture().encode())] * 2,
                        [('../outside', b'x')], [('LICENSE', b'no renderer')]):
            with self.subTest(entries=entries[:1]), self.assertRaises(ValueError):
                self.rewrite(entries)

    def test_version_is_local_development(self):
        self.assertEqual(update.VERSION, '0.0.3-perf.2')

    def test_display_version_never_changes_bundled_build_protocol(self):
        original = dict(schema=1, version='Pre-Alpha-0.0.1', source_sha256='old')
        recipe = update.performance_recipe(original, 'new')
        self.assertEqual(original['source_sha256'], 'old')
        self.assertEqual(recipe['version'], original['version'])
        self.assertEqual(recipe['package_version'], update.VERSION)
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp)
            (app / 'runtime').mkdir()
            (app / 'runtime/recipe.json').write_text(json.dumps(recipe))
            self.assertEqual(NativeBuild(app).recipe, recipe)

    def test_unknown_build_protocol_is_not_silently_relabelled(self):
        for original in (dict(schema=2, version='Pre-Alpha-0.0.1'),
                         dict(schema=1, version='Local-Development-0.0.3-perf.1')):
            with self.assertRaises(ValueError):
                update.performance_recipe(original, 'new')


if __name__ == '__main__':
    unittest.main()
