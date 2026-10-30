# SPDX-License-Identifier: MPL-2.0
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile
from review_distribution import review

try:
    from public_package import package
except ImportError:
    from package import package


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.root=Path(self.temporary.name)
        self.app=self.root/'Super Mario 64'
        (self.app/'runtime').mkdir(parents=True)
        (self.app/'runtime/source-material-review.json').write_text(json.dumps(dict(schema=1,complete=True)))
        for name in ('meta.json','SM64.sh','icon.webp','LICENSE','original/README.txt'):
            path=self.app/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('fixture')
        self.source([('src/engine.c',b'/* source */')])
        review(self.app)

    def tearDown(self):
        self.temporary.cleanup()

    def source(self,files):
        with tarfile.open(self.app/'runtime/source.tar.gz','w:gz') as archive:
            for name,data in files:
                member=tarfile.TarInfo(name)
                member.size=len(data)
                archive.addfile(member,io.BytesIO(data))

    def test_private_rom_cache_and_saves_are_excluded(self):
        for name in ('original/my.z64','.sm64-cache/ready/game','saves/save.dat'):
            path=self.app/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(b'private fixture')
        target=self.root/'public.lilpck'
        result=package(self.app,target)
        self.assertFalse(result['rom_included'])
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(set(archive.namelist()),{'meta.json','SM64.sh','icon.webp',
                'LICENSE','original/README.txt','runtime/source.tar.gz','runtime/distribution-manifest.json',
                'runtime/source-material-review.json'})
            self.assertIsNone(archive.testzip())

    def test_game_or_renderer_binary_in_source_rejected(self):
        self.source([('src/pc/gfx/hidden',b'\x7fELFprivate fixture')])
        with self.assertRaisesRegex(ValueError,'Unexpected source binary'):
            review(self.app)

    def test_extracted_rom_assets_rejected(self):
        self.source([('textures/mario.png',b'private fixture')])
        with self.assertRaisesRegex(ValueError,'Extracted data'):
            review(self.app)

    def test_unreviewed_runtime_game_is_rejected(self):
        (self.app/'runtime/game').write_bytes(b'\x7fELFprivate fixture')
        with self.assertRaisesRegex(ValueError,'Unexpected distribution file'):
            review(self.app)

    def test_changed_reviewed_input_fails_packaging(self):
        (self.app/'SM64.sh').write_text('changed fixture')
        with self.assertRaisesRegex(ValueError,'input changed'):
            package(self.app,self.root/'bad.lilpck')

    def test_fat_case_collisions_rejected_before_distribution(self):
        (self.app/'runtime/python').mkdir()
        (self.app/'runtime/python/A.py').write_text('fixture')
        (self.app/'runtime/python/a.py').write_text('fixture')
        if len(list((self.app/'runtime/python').iterdir()))!=2:
            self.skipTest('Requires a case-sensitive staging filesystem')
        with self.assertRaisesRegex(ValueError,'FAT case collisions'):
            review(self.app)


if __name__=='__main__': unittest.main()
