# SPDX-License-Identifier: MPL-2.0
import importlib.util,json,struct,tempfile,unittest,zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('alpha_image',Path(__file__).with_name('build_alpha_image.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class AlphaImageTests(unittest.TestCase):
    def test_paths(self):
        for name in ('/etc/shadow','../outside','a/../b','a\\b','a//b',''):
            with self.assertRaises(ValueError):m.relative(name)
        self.assertEqual(str(m.relative('generation-01/000001.bin')),'generation-01/000001.bin')
    def test_manifest(self):
        self.assertEqual(len(m.rows('D\t0755\tusr\t\nF\t0644\tusr/config\tgen/000001.bin\n')),2)
        for text in ('F\t0644\tx\t../secret\n','F\t0644\tx\ta\nD\t0755\tx\t\n','X\t0644\tx\ta\n'):
            with self.assertRaises(ValueError):m.rows(text)
    def test_experiments_not_generic_n64(self):
        self.assertTrue(m.excluded('usr/share/portsmoke/engines/sm64-byo-dev.peng'))
        self.assertTrue(m.excluded('usr/share/Daedalus-LilSmoke/app'))
        self.assertFalse(m.excluded('usr/lib/mupen64plus/libmupen64plus.so'))
    def test_factory_no_private_override(self):
        text=m.config('DISPLAY_WIDTH=320\nDISPLAY_WIDTH=640\nSSH_ENABLED=1\n# retained\n')
        self.assertEqual(text.count('DISPLAY_WIDTH='),1)
        for expected in ('DISPLAY_WIDTH="1280"','SSH_ENABLED="0"','SCAN_ROMS_ON_BOOT="1"','EMULATOR_snes="od:pocketsnes"','EMULATOR_psx="app:org.lilsmoke.pcsx4all"'):
            self.assertIn(expected,text)
        self.assertIn('# retained',text)
    def test_builtin_asset_check(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'app.lilpck'
            with zipfile.ZipFile(p,'w') as z:z.writestr('meta.json',json.dumps({'id':'org.lilsmoke.pcsx4all'}))
            self.assertEqual(m.audit_package(p)['id'],'org.lilsmoke.pcsx4all')
            with zipfile.ZipFile(p,'a') as z:z.writestr('runtime/game.z64',b'not shipped')
            with self.assertRaises(ValueError):m.audit_package(p)
    def test_primary_and_backup_free_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'test.fat';data=bytearray(128*512)
            data[13]=1;data[16]=2
            struct.pack_into('<H',data,14,8);struct.pack_into('<I',data,32,128)
            struct.pack_into('<I',data,36,1);struct.pack_into('<HH',data,48,1,6)
            struct.pack_into('<I',data,8*512+8,0x0fffffff)
            p.write_bytes(data);m.sync_fat_info(p);result=p.read_bytes()
            self.assertEqual(struct.unpack_from('<II',result,512+488),(117,3))
            self.assertEqual(result[512:1024],result[7*512:8*512])

if __name__=='__main__':unittest.main()
