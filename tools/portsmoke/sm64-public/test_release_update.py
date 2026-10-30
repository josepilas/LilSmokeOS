# SPDX-License-Identifier: MPL-2.0
import hashlib
import json
import unittest
import zipfile
from review_distribution import review, source_offer
from prepare_release import release_package
import test_package as fixtures
try:
    from public_package import package
except ImportError:
    from package import package


class ReleaseUpdateTests(unittest.TestCase):
    setUp = fixtures.DistributionTests.setUp
    tearDown = fixtures.DistributionTests.tearDown
    source = fixtures.DistributionTests.source

    def base(self):
        (self.app / 'meta.json').write_text(json.dumps(dict(id='org.lilsmoke.sm64', version='0.0.1-prep.8')))
        (self.app / 'runtime/recipe.json').write_text('{"private_build_identity":"unchanged-b9"}\n')
        (self.app / 'runtime/cook.py').write_text('# immutable runtime fixture\n')
        review(self.app)
        target = self.root / 'base.lilpck'
        package(self.app, target)
        return target

    def delivery(self):
        return dict(url='https://github.com/josepilas/SM64-LilSmoke/releases/download/Pre-Alpha-0.0.2/corresponding-source.zip',
                    sha256='1' * 64, bytes=12, asset='corresponding-source.zip')

    def test_release_updates_metadata_offer_without_invalidating_ready_recipe(self):
        base = self.base()
        output = self.root / 'release.lilpck'
        result = release_package(base, output, self.delivery(), '0.0.2', 'Pre-Alpha-0.0.2')
        self.assertTrue(result['operational_payload_unchanged'])
        with zipfile.ZipFile(base) as old, zipfile.ZipFile(output) as new:
            self.assertEqual(json.loads(new.read('meta.json'))['version'], '0.0.2')
            self.assertIn('/releases/tag/Pre-Alpha-0.0.2', new.read('SOURCE-OFFER.txt').decode())
            for name in old.namelist():
                if name not in result['changed_files']:
                    self.assertEqual(hashlib.sha256(old.read(name)).digest(), hashlib.sha256(new.read(name)).digest())
            self.assertEqual(old.read('runtime/recipe.json'), new.read('runtime/recipe.json'))
            self.assertIsNone(new.testzip())

    def test_unreviewed_rom_and_duplicate_members_fail(self):
        for duplicate in (False, True):
            base = self.base()
            with zipfile.ZipFile(base, 'a') as archive:
                archive.writestr('SM64.sh' if duplicate else 'original/private.z64', b'fixture')
            with self.assertRaises(ValueError):
                release_package(base, self.root / 'bad.lilpck', self.delivery(), '0.0.2', 'Pre-Alpha-0.0.2')
            base.unlink()

    def test_source_offer_rejects_unrelated_destination(self):
        with self.assertRaises(ValueError):
            source_offer(dict(url='https://example.invalid/corresponding-source.zip', sha256='0' * 64))


if __name__ == '__main__':
    unittest.main()
