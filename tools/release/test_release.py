# SPDX-License-Identifier: MPL-2.0
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from check_release import audit,artifact_reasons,ROOT,VERSION

class ReleaseTests(unittest.TestCase):
    def test_alpha_metadata(self):
        metadata=json.loads((ROOT/'release.json').read_text(encoding='utf-8'))
        self.assertEqual(VERSION,'Alpha-0.0.1')
        self.assertEqual(metadata['version'],VERSION)
        self.assertEqual(metadata['first_party_license'],'MPL-2.0')
        self.assertTrue(metadata['os_source_public'])
        self.assertNotIn('planned_future_source_license',metadata)
        self.assertEqual(audit([])['version'],VERSION)
    def setUp(self): self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()
    def file(self,name,data):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);return path
    def test_plain_notice(self): self.assertTrue(audit([self.file('LICENSE.txt',b'Public notice')])['passed'])
    def test_renamed_mbr(self):
        data=bytearray(512);data[510:512]=b'\x55\xaa';self.assertTrue(artifact_reasons(self.file('hidden.dat',data)))
    def test_stock_image(self):
        p=self.file('release.img',bytes(512));p.with_suffix('.img.json').write_text(json.dumps({'stock_hashes':{'kernel':'a'*64}}))
        self.assertFalse(audit([p])['passed'])
    def test_private_package(self):
        p=self.root/'app.lilpck'
        with zipfile.ZipFile(p,'w') as z:z.writestr('meta.json',json.dumps({'private_user_build':True}))
        self.assertTrue(artifact_reasons(p))
    def test_renamed_private_zip(self):
        p=self.root/'app.dat'
        with zipfile.ZipFile(p,'w') as z:z.writestr('runtime/build-info.json',json.dumps({'contains_user_rom_assets':True}))
        self.assertTrue(artifact_reasons(p))
    def test_rom_archive(self):
        p=self.root/'assets.zip'
        with zipfile.ZipFile(p,'w') as z:z.writestr('original/game.z64',b'private')
        self.assertTrue(artifact_reasons(p))
    def test_traversal(self):
        p=self.root/'bad.zip'
        with zipfile.ZipFile(p,'w') as z:z.writestr('../bad',b'bad')
        self.assertTrue(artifact_reasons(p))
    def test_unreviewed_compression(self): self.assertTrue(artifact_reasons(self.file('app.7z',b'7z')))
    def test_elf_renamed(self): self.assertTrue(artifact_reasons(self.file('app.dat',b'\x7fELF'+bytes(60))))
    def test_duplicate_metadata(self):
        p=self.file('bad.img',bytes(512));p.with_suffix('.img.json').write_text('{"stock_hashes":{},"stock_hashes":{}}')
        self.assertTrue(artifact_reasons(p))
    def test_empty_not_pass(self): self.assertFalse(audit([])['passed'])
    def test_binary_inside_package(self):
        p=self.root/'app.zip'
        with zipfile.ZipFile(p,'w') as z:z.writestr('runtime/bin/program',b'\x7fELF'+bytes(60))
        self.assertTrue(artifact_reasons(p))
    def test_renamed_rom(self): self.assertTrue(artifact_reasons(self.file('data.dat',b'\x80\x37\x12\x40'+bytes(512))))
    def test_renamed_compression(self): self.assertTrue(artifact_reasons(self.file('data.dat',b'7z\xbc\xaf\x27\x1c'+bytes(60))))
    def test_unknown_data_needs_review(self): self.assertTrue(artifact_reasons(self.file('game.dat',b'opaque game data')))
    def test_invalid_metadata_type(self):
        p=self.root/'bad.zip'
        with zipfile.ZipFile(p,'w') as z:z.writestr('meta.json','[]')
        self.assertTrue(artifact_reasons(p))

if __name__=='__main__': unittest.main()
