# SPDX-License-Identifier: MPL-2.0
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from review_distribution import review
try:
    from public_package import package
except ImportError:
    from package import package
from split_sources import split, SOURCE_PREFIX, SOURCE_URL
import test_package as fixtures


class SourceSplitTests(unittest.TestCase):
    def setUp(self):
        fixtures.DistributionTests.setUp(self)
        (self.app/'runtime/recipe.json').write_text(json.dumps(dict(fixture=True)))
        review(self.app)

    tearDown=fixtures.DistributionTests.tearDown
    source=fixtures.DistributionTests.source

    def test_sources_separate_without_changing_offline_runtime(self):
        materials=self.app/'runtime/corresponding-source'
        materials.mkdir()
        (materials/'dependency.c').write_text('/* matching source fixture */')
        review(self.app)
        original=self.root/'SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck'
        package(self.app,original)
        result=split(original,self.root/'lean')
        with zipfile.ZipFile(original) as old, zipfile.ZipFile(self.root/'lean'/original.name) as lean:
            for name in old.namelist():
                if name.startswith(SOURCE_PREFIX) or name in ('runtime/source-material-review.json','runtime/distribution-manifest.json'):
                    continue
                self.assertEqual(old.read(name),lean.read(name))
            self.assertIn(SOURCE_URL,lean.read('SOURCE-OFFER.txt').decode())
            self.assertFalse(any(name.startswith(SOURCE_PREFIX) for name in lean.namelist()))
            delivery=json.loads(lean.read('runtime/source-material-review.json'))['external_corresponding_source']
            self.assertEqual(delivery['sha256'],result['corresponding_source']['sha256'])
        with zipfile.ZipFile(self.root/'lean/corresponding-source.zip') as sources:
            self.assertEqual(sources.read(SOURCE_PREFIX+'dependency.c'),b'/* matching source fixture */')
            self.assertIsNone(sources.testzip())

    def test_split_rejects_unreviewed_extra_file(self):
        original=self.root/'SM64-LilSmoke-Pre-Alpha-0.0.1.lilpck'
        package(self.app,original)
        with zipfile.ZipFile(original,'a') as archive: archive.writestr('original/hidden.z64',b'private fixture')
        with self.assertRaisesRegex(ValueError,'Unreviewed'):
            split(original,self.root/'lean')


if __name__=='__main__': unittest.main()
